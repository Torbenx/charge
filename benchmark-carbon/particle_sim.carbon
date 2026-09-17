// A deterministic 2D rigid-body physics engine in fixed-point arithmetic.
//
// Everything in this file is integer maths. The language accepts no float
// literals, and a physics engine that has to be replayable across machines
// does not want them anyway: two runs of the same scene on two machines must
// produce bit-identical results, which is exactly what fixed point buys.
//
// The layout follows the usual pipeline:
//
//     integrate forces -> broad phase -> narrow phase -> solve -> integrate
//     velocities -> position correction
//
// Each stage is its own section below. The scalar, vector and matrix types at
// the top carry a full set of operator impls so that the maths in the later
// stages reads as ordinary algebra over chained member accesses, for example
//
//     body.velocity.x + contact.normal.x * impulse.magnitude
//
// rather than as a ladder of named helper calls.
namespace physics;

// --- Fixed-point configuration -------------------------------------------

// Number of fractional bits in a Fixed. One unit of the underlying integer is
// 1 / 65536 of a whole number, which gives roughly 4.5 decimal digits of
// fraction. That is enough for contact positions measured in metres while
// leaving 47 bits of integer range in a 64-bit accumulator.
let FIXED_SHIFT: int = 16;

// 2^16. The raw value of the number one. Every conversion in this file is a
// multiply or divide by this constant.
let FIXED_ONE: int = 65536;

// 2^15, one half. Used for the round-to-nearest step in multiply and divide.
let FIXED_HALF: int = 32768;

// 2^14, one quarter.
let FIXED_QUARTER: int = 16384;

// 2^16 - 1, the mask that isolates the fractional part of a raw value.
let FIXED_FRACTION_MASK: int = 65535;

// 2^32, the raw value of 65536.0. Products of two Fixed values are shifted
// back down by FIXED_SHIFT, so an intermediate may reach this magnitude
// multiplied by the operand range before the shift happens.
let FIXED_ONE_SQUARED: int = 4294967296;

// Largest and smallest representable raw values, leaving generous headroom so
// that a runaway simulation saturates instead of wrapping.
let FIXED_MAX_RAW: int = 140737488355328;
let FIXED_MIN_RAW: int = -140737488355328;

// pi, tau and half pi in raw fixed-point units:
//   pi      = 3.14159265 * 65536 = 205887
//   tau     = 6.28318531 * 65536 = 411775
//   half pi = 1.57079633 * 65536 = 102944
let FIXED_PI_RAW: int = 205887;
let FIXED_TAU_RAW: int = 411775;
let FIXED_HALF_PI_RAW: int = 102944;
let FIXED_QUARTER_PI_RAW: int = 51472;

// 1 / pi and 2 / pi, for range reduction without a division.
let FIXED_INV_PI_RAW: int = 20861;
let FIXED_TWO_OVER_PI_RAW: int = 41722;

// Small epsilons. LINEAR_EPSILON is about 1/1000 of a unit and is the
// threshold below which a length is treated as zero; ANGULAR_EPSILON is
// about 1/10000 of a radian.
let LINEAR_EPSILON_RAW: int = 66;
let ANGULAR_EPSILON_RAW: int = 7;

// Square of LINEAR_EPSILON, kept separately so that length comparisons can
// avoid the square root entirely.
let LINEAR_EPSILON_SQUARED_RAW: int = 1;

// Newton iteration budget for the square root and the reciprocal. Both
// converge quadratically, so six steps is far more than the 16 fractional
// bits require; the extra steps only cost when the initial guess is poor.
let SQRT_ITERATION_LIMIT: int = 6;
let RECIPROCAL_ITERATION_LIMIT: int = 5;

// Instrumentation counters. A profiler run reads these to see which stage of
// the pipeline dominated a frame.
var fixed_multiply_count: int = 0;
var fixed_divide_count: int = 0;
var fixed_sqrt_count: int = 0;
var narrow_phase_test_count: int = 0;
var broad_phase_pair_count: int = 0;
var solver_iteration_count: int = 0;

// The apostrophe, spelled as a string because the character literal form for
// it is not available.
let APOSTROPHE: string = "'";

// --- Small enumerations ---------------------------------------------------

// Three-way comparison, shared by the scalar and the vector types.
choice Ordering {
    Less,
    Equal,
    Greater,
}

// Which of the three shape representations a Shape holds. There is no sum
// type in the language, so a Shape carries one of each payload and this tag
// says which payload is live.
choice ShapeKind {
    Circle,
    Box,
    Polygon,
}

// How a body participates in the simulation.
choice BodyKind {
    // Infinite mass, never moved by the solver, but may be teleported.
    Static,
    // Infinite mass, moved by its own velocity, unaffected by impulses.
    Kinematic,
    // Full participant.
    Dynamic,
}

// Result of a narrow-phase query.
choice ContactState {
    // The shapes are apart.
    Separated,
    // The shapes touch but the manifold is empty, which happens for exact
    // vertex-on-vertex contact.
    Touching,
    // A usable manifold with at least one point.
    Penetrating,
}

// Which feature of a polygon produced the deepest separating axis.
choice FeatureKind {
    FaceA,
    FaceB,
    VertexA,
    VertexB,
}

// Joint solver status, reported per joint per substep.
choice JointStatus {
    Satisfied,
    Corrected,
    Broken,
}

// --- The fixed-point scalar ----------------------------------------------
//
// A Fixed is a single integer interpreted as a multiple of 1 / FIXED_ONE.
// Addition and subtraction are exact integer operations on the raw value;
// multiplication and division need a shift to put the binary point back where
// it belongs, and that shift is the only place rounding happens.
//
// The struct is deliberately one field wide so that it can be passed by value
// everywhere without anybody worrying about aliasing.
class Fixed {

    // --- Construction -----------------------------------------------------

    // Reinterpret a raw integer as a Fixed. This is the identity conversion
    // and does no scaling at all.
    fn set_raw(ref self, value: int) {
        self.raw = value;
    }

    // The whole number n.
    fn set_whole(ref self, n: int) {
        self.raw = n * FIXED_ONE;
    }

    // The exact rational numerator / denominator, rounded to nearest. The
    // multiply happens before the divide so that the full precision of the
    // ratio survives into the result.
    fn set_ratio(ref self, numerator: int, denominator: int) {
        if (denominator == 0) {
            self.raw = 0;
            return;
        }
        let scaled = numerator * FIXED_ONE;
        if ((scaled < 0) != (denominator < 0)) {
            self.raw = (scaled - denominator / 2) / denominator;
        } else {
            self.raw = (scaled + denominator / 2) / denominator;
        }
    }

    // --- Queries ----------------------------------------------------------

    fn is_zero(self) -> bool {
        return self.raw == 0;
    }

    fn is_negative(self) -> bool {
        return self.raw < 0;
    }

    fn is_positive(self) -> bool {
        return self.raw > 0;
    }

    // Truncation towards zero, which is what the machine divide already does.
    fn to_int(self) -> int {
        return self.raw / FIXED_ONE;
    }

    // Largest whole number not greater than self. Integer division rounds
    // towards zero, so negatives need the extra step.
    fn floor_int(self) -> int {
        if (self.raw >= 0) {
            return self.raw / FIXED_ONE;
        }
        return -((-self.raw + FIXED_FRACTION_MASK) / FIXED_ONE);
    }

    fn ceil_int(self) -> int {
        return -((-self.raw) / FIXED_ONE);
    }

    // Round half away from zero.
    fn round_int(self) -> int {
        if (self.raw >= 0) {
            return (self.raw + FIXED_HALF) / FIXED_ONE;
        }
        return -((-self.raw + FIXED_HALF) / FIXED_ONE);
    }

    // The fractional part, always in [0, 1).
    fn fraction(self) -> Fixed {
        var result: Fixed;
        result.raw = self.raw - self.floor_int() * FIXED_ONE;
        return result;
    }

    fn magnitude(self) -> Fixed {
        var result: Fixed;
        if (self.raw < 0) {
            result.raw = -self.raw;
        } else {
            result.raw = self.raw;
        }
        return result;
    }

    fn negated(self) -> Fixed {
        var result: Fixed;
        result.raw = -self.raw;
        return result;
    }

    // -1, 0 or +1 as a whole Fixed.
    fn sign_unit(self) -> Fixed {
        var result: Fixed;
        if (self.raw > 0) {
            result.raw = FIXED_ONE;
        } else {
            if (self.raw < 0) {
                result.raw = -FIXED_ONE;
            } else {
                result.raw = 0;
            }
        }
        return result;
    }

    fn compare(self, other: Fixed) -> Ordering {
        if (self.raw < other.raw) {
            return Ordering.Less;
        }
        if (self.raw > other.raw) {
            return Ordering.Greater;
        }
        return Ordering.Equal;
    }

    // --- Core arithmetic --------------------------------------------------

    fn added(self, other: Fixed) -> Fixed {
        var result: Fixed;
        result.raw = self.raw + other.raw;
        return result;
    }

    fn subtracted(self, other: Fixed) -> Fixed {
        var result: Fixed;
        result.raw = self.raw - other.raw;
        return result;
    }

    // Product with round-to-nearest. The intermediate .raw * other.raw is a
    // 2 * FIXED_SHIFT fractional-bit number; adding FIXED_HALF before the
    // shift rounds instead of truncating, which keeps the multiply unbiased
    // and stops a long chain of multiplies drifting towards zero.
    fn multiplied(self, other: Fixed) -> Fixed {
        fixed_multiply_count += 1;
        var result: Fixed;
        let product = self.raw * other.raw;
        if (product < 0) {
            result.raw = -((-product + FIXED_HALF) >> FIXED_SHIFT);
        } else {
            result.raw = (product + FIXED_HALF) >> FIXED_SHIFT;
        }
        return result;
    }

    // Quotient with round-to-nearest. The numerator is pre-shifted so that
    // the fractional bits survive the integer divide.
    fn divided(self, other: Fixed) -> Fixed {
        fixed_divide_count += 1;
        var result: Fixed;
        if (other.raw == 0) {
            if (self.raw >= 0) {
                result.raw = FIXED_MAX_RAW;
            } else {
                result.raw = FIXED_MIN_RAW;
            }
            return result;
        }
        let numerator = self.raw << FIXED_SHIFT;
        if ((numerator < 0) != (other.raw < 0)) {
            result.raw = (numerator - other.raw / 2) / other.raw;
        } else {
            result.raw = (numerator + other.raw / 2) / other.raw;
        }
        return result;
    }

    // Scaling by a whole number, which needs no shift and so no rounding.
    fn scaled_by_int(self, n: int) -> Fixed {
        var result: Fixed;
        result.raw = self.raw * n;
        return result;
    }

    fn divided_by_int(self, n: int) -> Fixed {
        var result: Fixed;
        if (n == 0) {
            result.raw = 0;
            return result;
        }
        result.raw = self.raw / n;
        return result;
    }

    // Self multiplied by itself. Worth a name of its own because squared
    // lengths appear on nearly every hot path in the narrow phase.
    fn squared(self) -> Fixed {
        return self.multiplied(self);
    }

    // --- Roots and reciprocals --------------------------------------------

    // Integer square root of the raw value, by Newton's method on integers.
    // sqrt(x) in fixed point is sqrt(raw / ONE) = sqrt(raw * ONE) / ONE, so
    // the raw value is pre-multiplied by FIXED_ONE before the integer root.
    fn square_root(self) -> Fixed {
        fixed_sqrt_count += 1;
        var result: Fixed;
        if (self.raw <= 0) {
            result.raw = 0;
            return result;
        }
        let target = self.raw * FIXED_ONE;
        var guess = self.raw;
        if (guess < FIXED_ONE) {
            guess = FIXED_ONE;
        }
        var step: int = 0;
        while (step < SQRT_ITERATION_LIMIT) {
            let next = (guess + target / guess) / 2;
            if (next == guess) {
                step = SQRT_ITERATION_LIMIT;
            } else {
                guess = next;
                step += 1;
            }
        }
        // One clean-up pass: Newton on integers can leave the estimate one
        // unit high but never more than one.
        while (guess > 1 and guess * guess > target) {
            guess -= 1;
        }
        result.raw = guess;
        return result;
    }

    // 1 / self, by Newton-Raphson on the reciprocal. The iteration is
    //     next = guess * (2 - self * guess)
    // which doubles the number of correct bits each pass. The initial guess
    // comes from the plain divide, so in practice one pass is enough and the
    // rest only tighten the last bit.
    fn reciprocal(self) -> Fixed {
        var one: Fixed;
        one.raw = FIXED_ONE;
        if (self.raw == 0) {
            var saturated: Fixed;
            saturated.raw = FIXED_MAX_RAW;
            return saturated;
        }
        var guess = one.divided(self);
        var two: Fixed;
        two.raw = FIXED_ONE * 2;
        var step: int = 0;
        while (step < RECIPROCAL_ITERATION_LIMIT) {
            let correction = two.subtracted(self.multiplied(guess));
            guess = guess.multiplied(correction);
            step += 1;
        }
        return guess;
    }

    // --- Clamping and blending --------------------------------------------

    fn clamped(self, low: Fixed, high: Fixed) -> Fixed {
        var result: Fixed;
        if (self.raw < low.raw) {
            result.raw = low.raw;
            return result;
        }
        if (self.raw > high.raw) {
            result.raw = high.raw;
            return result;
        }
        result.raw = self.raw;
        return result;
    }

    // Linear blend towards other. t is expected in [0, 1] but the formula is
    // fine outside that range, which is what extrapolating a contact point
    // between substeps needs.
    fn blended(self, other: Fixed, t: Fixed) -> Fixed {
        let delta = other.subtracted(self);
        return self.added(delta.multiplied(t));
    }

    // Move at most limit towards target. Used by the motor joints so that a
    // motor cannot overshoot its own set point in a single substep.
    fn stepped_towards(self, target: Fixed, limit: Fixed) -> Fixed {
        let delta = target.subtracted(self);
        if (delta.magnitude().raw <= limit.raw) {
            return target.added(zero_fixed());
        }
        return self.added(delta.sign_unit().multiplied(limit));
    }

    // The single raw integer, in units of 1 / FIXED_ONE.
    var raw: int = 0;
}

// --- Fixed constructors as free functions ---------------------------------
//
// Struct literals with designated arguments would work here, but a named
// constructor keeps the scale conversion visible at the call site, which is
// the single easiest thing to get wrong in a fixed-point codebase.

fn raw_fixed(value: int) -> Fixed {
    var result: Fixed;
    result.raw = value;
    return result;
}

fn whole(n: int) -> Fixed {
    var result: Fixed;
    result.raw = n * FIXED_ONE;
    return result;
}

fn ratio(numerator: int, denominator: int) -> Fixed {
    var result: Fixed;
    result.set_ratio(numerator, denominator);
    return result;
}

fn zero_fixed() -> Fixed {
    return raw_fixed(0);
}

fn one_fixed() -> Fixed {
    return raw_fixed(FIXED_ONE);
}

fn half_fixed() -> Fixed {
    return raw_fixed(FIXED_HALF);
}

fn pi_fixed() -> Fixed {
    return raw_fixed(FIXED_PI_RAW);
}

fn tau_fixed() -> Fixed {
    return raw_fixed(FIXED_TAU_RAW);
}

fn epsilon_fixed() -> Fixed {
    return raw_fixed(LINEAR_EPSILON_RAW);
}

// --- Fixed operator implementations ---------------------------------------
//
// These give the named methods above their infix spelling. Nothing else in
// this file calls Fixed.added or Fixed.multiplied directly; from here down
// the physics is written as ordinary algebra, which is the whole point of
// declaring them.

impl Fixed as Core.AddWith(Fixed) where .Result = Fixed {
  fn Op(self, other: Fixed) -> Fixed {
      return self.added(other);
  }
}
impl Fixed as Core.SubWith(Fixed) where .Result = Fixed {
  fn Op(self, other: Fixed) -> Fixed {
      return self.subtracted(other);
  }
}
impl Fixed as Core.MulWith(Fixed) where .Result = Fixed {
  fn Op(self, other: Fixed) -> Fixed {
      return self.multiplied(other);
  }
}
impl Fixed as Core.DivWith(Fixed) where .Result = Fixed {
  fn Op(self, other: Fixed) -> Fixed {
      return self.divided(other);
  }
}

// Remainder in fixed point: the raw values divide exactly, so the machine
// remainder is already correctly scaled.
impl Fixed as Core.ModWith(Fixed) where .Result = Fixed {
  fn Op(self, other: Fixed) -> Fixed {
      if (other.raw == 0) {
          return zero_fixed();
      }
      return raw_fixed(self.raw % other.raw);
  }
}
impl Fixed as Core.OrderedWith(Fixed) {
  fn Less(self, other: Fixed) -> bool {
      return self.raw < other.raw;
  }

  fn LessOrEquivalent(self, other: Fixed) -> bool {
      return self.raw <= other.raw;
  }

  fn Greater(self, other: Fixed) -> bool {
      return self.raw > other.raw;
  }

  fn GreaterOrEquivalent(self, other: Fixed) -> bool {
      return self.raw >= other.raw;
  }
}
impl Fixed as Core.EqWith(Fixed) {
  fn Equal(self, other: Fixed) -> bool {
      return self.raw == other.raw;
  }

  fn NotEqual(self, other: Fixed) -> bool {
      return self.raw != other.raw;
  }
}

// Unary negation. Exact: no rounding is involved.
impl Fixed as Core.Negate where .Result = Fixed {
  fn Op(self) -> Fixed {
      return raw_fixed(-self.raw);
  }
}

// Mixed scalar forms, so that a whole-number coefficient does not have to be
// wrapped at every call site.
impl Fixed as Core.MulWith(int) where .Result = Fixed {
  fn Op(self, other: int) -> Fixed {
      return raw_fixed(self.raw * other);
  }
}
impl Fixed as Core.DivWith(int) where .Result = Fixed {
  fn Op(self, other: int) -> Fixed {
      return self.divided_by_int(other);
  }
}

// --- Free-function scalar helpers -----------------------------------------

fn fixed_min(a: Fixed, b: Fixed) -> Fixed {
    if (a.raw < b.raw) {
        return raw_fixed(a.raw);
    }
    return raw_fixed(b.raw);
}

fn fixed_max(a: Fixed, b: Fixed) -> Fixed {
    if (a.raw > b.raw) {
        return raw_fixed(a.raw);
    }
    return raw_fixed(b.raw);
}

fn fixed_abs(a: Fixed) -> Fixed {
    return a.magnitude();
}

fn fixed_clamp(value: Fixed, low: Fixed, high: Fixed) -> Fixed {
    return value.clamped(low, high);
}

fn fixed_sqrt(a: Fixed) -> Fixed {
    return a.square_root();
}

// True when the two values are within tolerance of each other. Every
// floating-comparison instinct in a physics engine should route through here.
fn fixed_near(a: Fixed, b: Fixed, tolerance: Fixed) -> bool {
    return (a - b).magnitude() <= tolerance;
}

fn fixed_near_zero(a: Fixed) -> bool {
    return a.magnitude().raw <= LINEAR_EPSILON_RAW;
}

// --- Angle handling -------------------------------------------------------

// Fold an angle into (-pi, pi]. The loop runs at most a couple of times for
// any angle a simulation actually produces; the guard is there for the case
// where an angular velocity has already exploded.
fn normalise_angle(angle: Fixed) -> Fixed {
    var result = raw_fixed(angle.raw);
    var guard: int = 0;
    while (result.raw > FIXED_PI_RAW and guard < 64) {
        result.raw -= FIXED_TAU_RAW;
        guard += 1;
    }
    while (result.raw <= -FIXED_PI_RAW and guard < 64) {
        result.raw += FIXED_TAU_RAW;
        guard += 1;
    }
    return result;
}

// Signed shortest angular distance from a to b, in (-pi, pi].
fn angle_delta(a: Fixed, b: Fixed) -> Fixed {
    return normalise_angle(b - a);
}

// --- Trigonometry ---------------------------------------------------------
//
// sin is approximated by the Bhaskara I rational form on [0, pi]:
//
//     sin(x) = 16 x (pi - x) / (5 pi^2 - 4 x (pi - x))
//
// which is accurate to about 1 part in 1000 across the whole half period,
// comfortably better than the 16 fractional bits can express near the peak
// and far cheaper than a Taylor series. Symmetry handles the other half.
fn fixed_sin(angle: Fixed) -> Fixed {
    let folded = normalise_angle(angle);
    var x = raw_fixed(folded.raw);
    var negate: bool = false;
    if (x.raw < 0) {
        x.raw = -x.raw;
        negate = true;
    }
    let pi = pi_fixed();
    let gap = pi - x;
    let product = x * gap;
    let numerator = product * 16;
    let denominator = pi * pi * 5 - product * 4;
    var result = numerator / denominator;
    if (negate) {
        result = -result;
    }
    return result;
}

fn fixed_cos(angle: Fixed) -> Fixed {
    return fixed_sin(angle + raw_fixed(FIXED_HALF_PI_RAW));
}

fn fixed_tan(angle: Fixed) -> Fixed {
    let c = fixed_cos(angle);
    if (c.magnitude().raw < LINEAR_EPSILON_RAW) {
        if (fixed_sin(angle).raw >= 0) {
            return raw_fixed(FIXED_MAX_RAW);
        }
        return raw_fixed(FIXED_MIN_RAW);
    }
    return fixed_sin(angle) / c;
}

// atan on [0, 1], by the standard minimax cubic
//
//     atan(x) = x * (pi/4) - x (|x| - 1) (0.2447 + 0.0663 |x|)
//
// with the two coefficients written as fixed-point raw values:
//     0.2447 * 65536 = 16035
//     0.0663 * 65536 = 4345
fn atan_unit(x: Fixed) -> Fixed {
    let magnitude = x.magnitude();
    let c1 = raw_fixed(16035);
    let c2 = raw_fixed(4345);
    let quarter_pi = raw_fixed(FIXED_QUARTER_PI_RAW);
    return x * quarter_pi - x * (magnitude - one_fixed()) * (c1 + c2 * magnitude);
}

// Full-circle arctangent, with the usual quadrant table. Returns a value in
// (-pi, pi].
fn fixed_atan2(y: Fixed, x: Fixed) -> Fixed {
    if (x.raw == 0 and y.raw == 0) {
        return zero_fixed();
    }
    let ax = x.magnitude();
    let ay = y.magnitude();
    if (ax >= ay) {
        let base_ = atan_unit(y / x);
        if (x.raw >= 0) {
            return base_;
        }
        if (y.raw >= 0) {
            return base_ + pi_fixed();
        }
        return base_ - pi_fixed();
    }
    let base_ = raw_fixed(FIXED_HALF_PI_RAW) - atan_unit(x / y);
    if (y.raw >= 0) {
        return base_;
    }
    return base_ - pi_fixed();
}

// asin on [-1, 1], derived from atan2 so that only one approximation has to
// be trusted. asin(x) = atan2(x, sqrt(1 - x^2)).
fn fixed_asin(x: Fixed) -> Fixed {
    let clamped = x.clamped(-one_fixed(), one_fixed());
    let complement = (one_fixed() - clamped * clamped).square_root();
    return fixed_atan2(clamped, complement);
}

fn fixed_acos(x: Fixed) -> Fixed {
    return raw_fixed(FIXED_HALF_PI_RAW) - fixed_asin(x);
}

// --- Vec2 -----------------------------------------------------------------
//
// The two-dimensional vector. Every component is a Fixed, so a Vec2 is two
// integers wide and is passed by value throughout.
//
// The cross product of two 2D vectors is a scalar (the z component of the 3D
// cross product) and the cross product of a scalar and a vector is a vector.
// Both spellings appear constantly in the angular parts of the solver, so
// both get a name here.
class Vec2 {

    fn set(ref self, nx: Fixed, ny: Fixed) {
        self.x = nx + zero_fixed();
        self.y = ny + zero_fixed();
    }

    fn set_zero(ref self) {
        self.x = zero_fixed();
        self.y = zero_fixed();
    }

    fn is_zero(self) -> bool {
        return self.x.raw == 0 and self.y.raw == 0;
    }

    // Dot product. This one line is the workhorse of the entire solver: every
    // relative velocity along a contact normal is a dot product.
    fn dot(self, other: Vec2) -> Fixed {
        return self.x * other.x + self.y * other.y;
    }

    // Scalar cross product, positive when other is counter-clockwise of self.
    fn cross(self, other: Vec2) -> Fixed {
        return self.x * other.y - self.y * other.x;
    }

    // Cross of self with a scalar, which rotates self by -90 degrees and
    // scales it. Equivalent to cross(self, s * z_hat).
    fn cross_scalar(self, s: Fixed) -> Vec2 {
        return make_vec2(self.y * s, -self.x * s);
    }

    fn length_squared(self) -> Fixed {
        return self.x * self.x + self.y * self.y;
    }

    fn length(self) -> Fixed {
        return (self.x * self.x + self.y * self.y).square_root();
    }

    // Manhattan length, used by the broad phase where an exact length would
    // be wasted work.
    fn manhattan_length(self) -> Fixed {
        return self.x.magnitude() + self.y.magnitude();
    }

    fn chebyshev_length(self) -> Fixed {
        return fixed_max(self.x.magnitude(), self.y.magnitude());
    }

    // Unit vector in the same direction, or the zero vector when self is too
    // short to have a direction at all.
    fn normalised(self) -> Vec2 {
        let len = self.length();
        if (len.raw <= LINEAR_EPSILON_RAW) {
            return make_vec2(zero_fixed(), zero_fixed());
        }
        let inverse = len.reciprocal();
        return make_vec2(self.x * inverse, self.y * inverse);
    }

    // Normalise in place and report the length that was removed. The solver
    // wants both, and computing the length twice is the sort of thing that
    // shows up in a profile.
    fn normalise_in_place(ref self) -> Fixed {
        let len = self.length();
        if (len.raw <= LINEAR_EPSILON_RAW) {
            self.x = zero_fixed();
            self.y = zero_fixed();
            return zero_fixed();
        }
        let inverse = len.reciprocal();
        self.x = self.x * inverse;
        self.y = self.y * inverse;
        return len;
    }

    // Rotated by +90 degrees: (x, y) becomes (-y, x).
    fn perpendicular(self) -> Vec2 {
        return make_vec2(-self.y, self.x);
    }

    // Rotated by -90 degrees.
    fn perpendicular_cw(self) -> Vec2 {
        return make_vec2(self.y, -self.x);
    }

    // Rotate by an arbitrary angle, given its sine and cosine. Passing the
    // pair in rather than the angle lets a caller hoist the trig out of a
    // loop over many vertices.
    fn rotated_by(self, cosine: Fixed, sine: Fixed) -> Vec2 {
        return make_vec2(self.x * cosine - self.y * sine, self.x * sine + self.y * cosine);
    }

    fn rotated(self, angle: Fixed) -> Vec2 {
        return self.rotated_by(fixed_cos(angle), fixed_sin(angle));
    }

    fn angle(self) -> Fixed {
        return fixed_atan2(self.y, self.x);
    }

    // Componentwise minimum and maximum, which is how an AABB grows.
    fn componentwise_min(self, other: Vec2) -> Vec2 {
        return make_vec2(fixed_min(self.x, other.x), fixed_min(self.y, other.y));
    }

    fn componentwise_max(self, other: Vec2) -> Vec2 {
        return make_vec2(fixed_max(self.x, other.x), fixed_max(self.y, other.y));
    }

    fn componentwise_multiply(self, other: Vec2) -> Vec2 {
        return make_vec2(self.x * other.x, self.y * other.y);
    }

    fn absolute(self) -> Vec2 {
        return make_vec2(self.x.magnitude(), self.y.magnitude());
    }

    fn negated(self) -> Vec2 {
        return make_vec2(-self.x, -self.y);
    }

    // Distance and squared distance to another point.
    fn distance_to(self, other: Vec2) -> Fixed {
        let dx = other.x - self.x;
        let dy = other.y - self.y;
        return (dx * dx + dy * dy).square_root();
    }

    fn distance_squared_to(self, other: Vec2) -> Fixed {
        let dx = other.x - self.x;
        let dy = other.y - self.y;
        return dx * dx + dy * dy;
    }

    // Linear blend from self to other.
    fn blended(self, other: Vec2, t: Fixed) -> Vec2 {
        return make_vec2(self.x + (other.x - self.x) * t, self.y + (other.y - self.y) * t);
    }

    // Reflect across a unit normal: v - 2 (v . n) n.
    fn reflected(self, normal: Vec2) -> Vec2 {
        let scale = self.dot(normal) * 2;
        return make_vec2(self.x - normal.x * scale, self.y - normal.y * scale);
    }

    // Component of self along a unit direction, as a vector.
    fn projected_onto(self, direction: Vec2) -> Vec2 {
        let amount = self.dot(direction);
        return make_vec2(direction.x * amount, direction.y * amount);
    }

    // The part of self that is orthogonal to direction.
    fn rejected_from(self, direction: Vec2) -> Vec2 {
        let along = self.projected_onto(direction);
        return make_vec2(self.x - along.x, self.y - along.y);
    }

    // Clamp the length without changing the direction. Used to cap velocity
    // so that one bad frame cannot fling a body across the world.
    fn clamped_length(self, limit: Fixed) -> Vec2 {
        let squared = self.length_squared();
        if (squared <= limit * limit) {
            return make_vec2(self.x, self.y);
        }
        let unit = self.normalised();
        return make_vec2(unit.x * limit, unit.y * limit);
    }

    fn scaled(self, factor: Fixed) -> Vec2 {
        return make_vec2(self.x * factor, self.y * factor);
    }

    var x: Fixed;
    var y: Fixed;
}

fn make_vec2(x: Fixed, y: Fixed) -> Vec2 {
    var result: Vec2;
    result.x = x + zero_fixed();
    result.y = y + zero_fixed();
    return result;
}

fn vec2_from_ints(x: int, y: int) -> Vec2 {
    return make_vec2(whole(x), whole(y));
}

fn vec2_zero() -> Vec2 {
    return make_vec2(zero_fixed(), zero_fixed());
}

fn vec2_unit_x() -> Vec2 {
    return make_vec2(one_fixed(), zero_fixed());
}

fn vec2_unit_y() -> Vec2 {
    return make_vec2(zero_fixed(), one_fixed());
}

// The unit vector at the given angle.
fn vec2_from_angle(angle: Fixed) -> Vec2 {
    return make_vec2(fixed_cos(angle), fixed_sin(angle));
}

// --- Vec2 operator implementations ----------------------------------------

impl Vec2 as Core.AddWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return make_vec2(self.x + other.x, self.y + other.y);
  }
}
impl Vec2 as Core.SubWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return make_vec2(self.x - other.x, self.y - other.y);
  }
}

// Vector times scalar. The reverse order is declared just below so that both
// `n * s` and `s * n` read naturally in the impulse expressions.
impl Vec2 as Core.MulWith(Fixed) where .Result = Vec2 {
  fn Op(self, other: Fixed) -> Vec2 {
      return make_vec2(self.x * other, self.y * other);
  }
}
impl Fixed as Core.MulWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return make_vec2(other.x * self, other.y * self);
  }
}

// Vector times integer, for the many places where the coefficient is a small
// whole number such as the 2 in a reflection.
impl Vec2 as Core.MulWith(int) where .Result = Vec2 {
  fn Op(self, other: int) -> Vec2 {
      return make_vec2(self.x * other, self.y * other);
  }
}
impl Vec2 as Core.DivWith(Fixed) where .Result = Vec2 {
  fn Op(self, other: Fixed) -> Vec2 {
      return make_vec2(self.x / other, self.y / other);
  }
}
impl Vec2 as Core.DivWith(int) where .Result = Vec2 {
  fn Op(self, other: int) -> Vec2 {
      return make_vec2(self.x / other, self.y / other);
  }
}

// Componentwise product. Spelled with % rather than * so that it can never be
// confused with a scaling; a physics reader seeing v % w knows both operands
// are vectors.
impl Vec2 as Core.ModWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return make_vec2(self.x * other.x, self.y * other.y);
  }
}
impl Vec2 as Core.EqWith(Vec2) {
  fn Equal(self, other: Vec2) -> bool {
      return self.x.raw == other.x.raw and self.y.raw == other.y.raw;
  }

  fn NotEqual(self, other: Vec2) -> bool {
      return self.x.raw != other.x.raw or self.y.raw != other.y.raw;
  }
}

// Ordering by length, which gives sort() something to work with when the
// broad phase wants its pair list in a deterministic order.
impl Vec2 as Core.OrderedWith(Vec2) {
  fn Less(self, other: Vec2) -> bool {
      return self.length_squared() < other.length_squared();
  }

  fn LessOrEquivalent(self, other: Vec2) -> bool {
      return self.length_squared() <= other.length_squared();
  }

  fn Greater(self, other: Vec2) -> bool {
      return self.length_squared() > other.length_squared();
  }

  fn GreaterOrEquivalent(self, other: Vec2) -> bool {
      return self.length_squared() >= other.length_squared();
  }
}
impl Vec2 as Core.Negate where .Result = Vec2 {
  fn Op(self) -> Vec2 {
      return make_vec2(-self.x, -self.y);
  }
}

// --- Vec2 free functions --------------------------------------------------

fn dot(a: Vec2, b: Vec2) -> Fixed {
    return a.x * b.x + a.y * b.y;
}

fn cross(a: Vec2, b: Vec2) -> Fixed {
    return a.x * b.y - a.y * b.x;
}

// s cross v, the vector form. Rotates v by +90 degrees and scales by s.
fn cross_scalar_vec(s: Fixed, v: Vec2) -> Vec2 {
    return make_vec2(-s * v.y, s * v.x);
}

// v cross s, the other vector form.
fn cross_vec_scalar(v: Vec2, s: Fixed) -> Vec2 {
    return make_vec2(s * v.y, -s * v.x);
}

fn vec2_lerp(a: Vec2, b: Vec2, t: Fixed) -> Vec2 {
    return a + (b - a) * t;
}

// The point on segment [a, b] closest to p, and the parameter that produced
// it. Both the circle-polygon test and the distance joint want this.
fn closest_point_on_segment(a: Vec2, b: Vec2, p: Vec2) -> Vec2 {
    let edge = b - a;
    let length_squared = edge.length_squared();
    if (length_squared.raw <= LINEAR_EPSILON_RAW) {
        return make_vec2(a.x, a.y);
    }
    let t = ((p - a).dot(edge) / length_squared).clamped(zero_fixed(), one_fixed());
    return a + edge * t;
}

fn segment_parameter(a: Vec2, b: Vec2, p: Vec2) -> Fixed {
    let edge = b - a;
    let length_squared = edge.length_squared();
    if (length_squared.raw <= LINEAR_EPSILON_RAW) {
        return zero_fixed();
    }
    return ((p - a).dot(edge) / length_squared).clamped(zero_fixed(), one_fixed());
}

// Signed area of the triangle abc, positive when the winding is
// counter-clockwise. Used by the polygon builder to reject a bad winding.
fn triangle_area_twice(a: Vec2, b: Vec2, c: Vec2) -> Fixed {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// --- Mat2 -----------------------------------------------------------------
//
// A 2x2 matrix stored as two column vectors. Columns rather than rows because
// the columns of a rotation matrix are the images of the basis vectors, which
// makes `m * v` a two-term combination of the columns and makes building a
// rotation from a sine and cosine obvious.
//
//     | col_x.x  col_y.x |
//     | col_x.y  col_y.y |
class Mat2 {

    fn set_identity(ref self) {
        self.col_x = make_vec2(one_fixed(), zero_fixed());
        self.col_y = make_vec2(zero_fixed(), one_fixed());
    }

    fn set_rotation(ref self, angle: Fixed) {
        let c = fixed_cos(angle);
        let s = fixed_sin(angle);
        self.col_x = make_vec2(c, s);
        self.col_y = make_vec2(-s, c);
    }

    fn set_scale(ref self, sx: Fixed, sy: Fixed) {
        self.col_x = make_vec2(sx, zero_fixed());
        self.col_y = make_vec2(zero_fixed(), sy);
    }

    // --- Element access ---------------------------------------------------

    fn element(self, row: int, column: int) -> Fixed {
        if (column == 0) {
            if (row == 0) {
                return self.col_x.x + zero_fixed();
            }
            return self.col_x.y + zero_fixed();
        }
        if (row == 0) {
            return self.col_y.x + zero_fixed();
        }
        return self.col_y.y + zero_fixed();
    }

    fn row_x(self) -> Vec2 {
        return make_vec2(self.col_x.x, self.col_y.x);
    }

    fn row_y(self) -> Vec2 {
        return make_vec2(self.col_x.y, self.col_y.y);
    }

    // --- Products ---------------------------------------------------------

    // m * v, written out as a combination of the columns.
    fn transform(self, v: Vec2) -> Vec2 {
        return make_vec2(self.col_x.x * v.x + self.col_y.x * v.y, self.col_x.y * v.x + self.col_y.y * v.y);
    }

    // m^T * v. For a rotation this is the inverse transform, which is how a
    // world-space vector is pulled into a body's local frame.
    fn transform_transposed(self, v: Vec2) -> Vec2 {
        return make_vec2(self.col_x.x * v.x + self.col_x.y * v.y, self.col_y.x * v.x + self.col_y.y * v.y);
    }

    fn multiplied(self, other: Mat2) -> Mat2 {
        return make_mat2(self.transform(other.col_x), self.transform(other.col_y));
    }

    fn transposed(self) -> Mat2 {
        return make_mat2(make_vec2(self.col_x.x, self.col_y.x), make_vec2(self.col_x.y, self.col_y.y));
    }

    // --- Invariants -------------------------------------------------------

    fn determinant(self) -> Fixed {
        return self.col_x.x * self.col_y.y - self.col_y.x * self.col_x.y;
    }

    fn trace(self) -> Fixed {
        return self.col_x.x + self.col_y.y;
    }

    // Inverse by the adjugate over the determinant. A singular matrix returns
    // the zero matrix rather than saturating, so a caller that forgets to
    // check gets an obviously wrong answer instead of a plausible one.
    fn inverted(self) -> Mat2 {
        let d = self.determinant();
        if (d.magnitude().raw <= LINEAR_EPSILON_RAW) {
            return make_mat2(vec2_zero(), vec2_zero());
        }
        let inverse = d.reciprocal();
        return make_mat2(make_vec2(self.col_y.y * inverse, -self.col_x.y * inverse),
                         make_vec2(-self.col_y.x * inverse, self.col_x.x * inverse));
    }

    // Solve m * result = b directly by Cramer's rule, which is both cheaper
    // and better conditioned than forming the inverse. The two-point contact
    // solver calls this for its block solve.
    fn solve(self, b: Vec2) -> Vec2 {
        let d = self.col_x.x * self.col_y.y - self.col_y.x * self.col_x.y;
        if (d.magnitude().raw <= LINEAR_EPSILON_RAW) {
            return vec2_zero();
        }
        let inverse = d.reciprocal();
        return make_vec2((self.col_y.y * b.x - self.col_y.x * b.y) * inverse,
                         (self.col_x.x * b.y - self.col_x.y * b.x) * inverse);
    }

    // The rotation angle this matrix represents, assuming it is a rotation.
    fn angle(self) -> Fixed {
        return fixed_atan2(self.col_x.y, self.col_x.x);
    }

    // A rotation matrix has orthonormal columns; drift from repeated
    // composition is removed by re-normalising the first column and
    // rebuilding the second from it.
    fn orthonormalised(self) -> Mat2 {
        let first = self.col_x.normalised();
        return make_mat2(first, first.perpendicular());
    }

    fn is_identity(self) -> bool {
        return self.col_x.x.raw == FIXED_ONE and self.col_x.y.raw == 0
            and self.col_y.x.raw == 0 and self.col_y.y.raw == FIXED_ONE;
    }

    fn negated(self) -> Mat2 {
        return make_mat2(-self.col_x, -self.col_y);
    }

    fn scaled(self, factor: Fixed) -> Mat2 {
        return make_mat2(self.col_x * factor, self.col_y * factor);
    }

    var col_x: Vec2;
    var col_y: Vec2;
}

fn make_mat2(col_x: Vec2, col_y: Vec2) -> Mat2 {
    var result: Mat2;
    result.col_x = col_x + vec2_zero();
    result.col_y = col_y + vec2_zero();
    return result;
}

fn mat2_identity() -> Mat2 {
    return make_mat2(vec2_unit_x(), vec2_unit_y());
}

fn mat2_zero() -> Mat2 {
    return make_mat2(vec2_zero(), vec2_zero());
}

fn mat2_rotation(angle: Fixed) -> Mat2 {
    let c = fixed_cos(angle);
    let s = fixed_sin(angle);
    return make_mat2(make_vec2(c, s), make_vec2(-s, c));
}

// The outer product a b^T, which is how a rank-one correction to an inertia
// tensor is built.
fn mat2_outer(a: Vec2, b: Vec2) -> Mat2 {
    return make_mat2(make_vec2(a.x * b.x, a.y * b.x), make_vec2(a.x * b.y, a.y * b.y));
}

// --- Mat2 operator implementations ----------------------------------------

impl Mat2 as Core.AddWith(Mat2) where .Result = Mat2 {
  fn Op(self, other: Mat2) -> Mat2 {
      return make_mat2(self.col_x + other.col_x, self.col_y + other.col_y);
  }
}
impl Mat2 as Core.SubWith(Mat2) where .Result = Mat2 {
  fn Op(self, other: Mat2) -> Mat2 {
      return make_mat2(self.col_x - other.col_x, self.col_y - other.col_y);
  }
}

// Matrix times matrix, in the usual right-to-left composition order: the
// result applies r first and then l.
impl Mat2 as Core.MulWith(Mat2) where .Result = Mat2 {
  fn Op(self, other: Mat2) -> Mat2 {
      return make_mat2(self.transform(other.col_x), self.transform(other.col_y));
  }
}

// Matrix times vector.
impl Mat2 as Core.MulWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return make_vec2(self.col_x.x * other.x + self.col_y.x * other.y, self.col_x.y * other.x + self.col_y.y * other.y);
  }
}

// Row vector times matrix, which is the transposed transform.
impl Vec2 as Core.MulWith(Mat2) where .Result = Vec2 {
  fn Op(self, other: Mat2) -> Vec2 {
      return make_vec2(other.col_x.x * self.x + other.col_x.y * self.y, other.col_y.x * self.x + other.col_y.y * self.y);
  }
}
impl Mat2 as Core.MulWith(Fixed) where .Result = Mat2 {
  fn Op(self, other: Fixed) -> Mat2 {
      return make_mat2(self.col_x * other, self.col_y * other);
  }
}
impl Fixed as Core.MulWith(Mat2) where .Result = Mat2 {
  fn Op(self, other: Mat2) -> Mat2 {
      return make_mat2(other.col_x * self, other.col_y * self);
  }
}
impl Mat2 as Core.DivWith(Fixed) where .Result = Mat2 {
  fn Op(self, other: Fixed) -> Mat2 {
      return make_mat2(self.col_x / other, self.col_y / other);
  }
}
impl Mat2 as Core.EqWith(Mat2) {
  fn Equal(self, other: Mat2) -> bool {
      return self.col_x == other.col_x and self.col_y == other.col_y;
  }

  fn NotEqual(self, other: Mat2) -> bool {
      return self.col_x != other.col_x or self.col_y != other.col_y;
  }
}

// Ordering by determinant, so that a degenerate matrix sorts below a healthy
// one. Only the debug tooling uses this.
impl Mat2 as Core.OrderedWith(Mat2) {
  fn Less(self, other: Mat2) -> bool {
      return self.determinant() < other.determinant();
  }

  fn Greater(self, other: Mat2) -> bool {
      return self.determinant() > other.determinant();
  }
}
impl Mat2 as Core.Negate where .Result = Mat2 {
  fn Op(self) -> Mat2 {
      return make_mat2(-self.col_x, -self.col_y);
  }
}

// --- Transform ------------------------------------------------------------
//
// A rigid transform: a rotation and a translation, with the rotation cached
// both as an angle and as the matrix built from it. The matrix is what every
// vertex transform actually uses; the angle is what the integrator advances.
class Transform {

    fn set_identity(ref self) {
        self.position = vec2_zero();
        self.angle = zero_fixed();
        self.rotation = mat2_identity();
    }

    // Rebuild the cached matrix after the angle has been integrated. This is
    // the only place the trig functions are called during a step.
    fn refresh(ref self) {
        self.angle = normalise_angle(self.angle);
        self.rotation = mat2_rotation(self.angle);
    }

    fn set_angle(ref self, value: Fixed) {
        self.angle = normalise_angle(value);
        self.rotation = mat2_rotation(self.angle);
    }

    // Local point to world point.
    fn apply(self, local: Vec2) -> Vec2 {
        return self.position + self.rotation * local;
    }

    // World point back to local.
    fn unapply(self, world: Vec2) -> Vec2 {
        return self.rotation.transform_transposed(world - self.position);
    }

    // Local direction to world direction: no translation.
    fn apply_direction(self, local: Vec2) -> Vec2 {
        return self.rotation * local;
    }

    fn unapply_direction(self, world: Vec2) -> Vec2 {
        return self.rotation.transform_transposed(world);
    }

    // The transform that undoes self.
    fn inverted(self) -> Transform {
        var result: Transform;
        result.angle = -self.angle;
        result.rotation = self.rotation.transposed();
        result.position = result.rotation * (-self.position);
        return result;
    }

    // self after other, so that applying the result equals applying other and
    // then applying self.
    fn composed(self, other: Transform) -> Transform {
        var result: Transform;
        result.angle = normalise_angle(self.angle + other.angle);
        result.rotation = self.rotation * other.rotation;
        result.position = self.position + self.rotation * other.position;
        return result;
    }

    var position: Vec2;
    var angle: Fixed;
    var rotation: Mat2;
}

fn make_transform(position: Vec2, angle: Fixed) -> Transform {
    var result: Transform;
    result.position = position + vec2_zero();
    result.angle = normalise_angle(angle);
    result.rotation = mat2_rotation(result.angle);
    return result;
}

fn transform_identity() -> Transform {
    return make_transform(vec2_zero(), zero_fixed());
}

// Composition, spelled infix so that a chain of frames reads left to right.
impl Transform as Core.MulWith(Transform) where .Result = Transform {
  fn Op(self, other: Transform) -> Transform {
      return self.composed(other);
  }
}

// Transform applied to a point.
impl Transform as Core.MulWith(Vec2) where .Result = Vec2 {
  fn Op(self, other: Vec2) -> Vec2 {
      return self.position + self.rotation * other;
  }
}
impl Transform as Core.EqWith(Transform) {
  fn Equal(self, other: Transform) -> bool {
      return self.position == other.position and self.angle == other.angle;
  }
}

// --- Axis-aligned bounding box --------------------------------------------
//
// Stored as a min and a max corner. An empty box is represented by a min that
// is greater than its max on at least one axis, which makes the merge of an
// empty box with anything the identity.
class AABB {

    fn set_empty(ref self) {
        self.lower = make_vec2(raw_fixed(FIXED_MAX_RAW), raw_fixed(FIXED_MAX_RAW));
        self.upper = make_vec2(raw_fixed(FIXED_MIN_RAW), raw_fixed(FIXED_MIN_RAW));
    }

    fn is_empty(self) -> bool {
        return self.lower.x > self.upper.x or self.lower.y > self.upper.y;
    }

    fn width(self) -> Fixed {
        return self.upper.x - self.lower.x;
    }

    fn height(self) -> Fixed {
        return self.upper.y - self.lower.y;
    }

    fn extents(self) -> Vec2 {
        return self.upper - self.lower;
    }

    fn half_extents(self) -> Vec2 {
        return (self.upper - self.lower) / 2;
    }

    fn centre(self) -> Vec2 {
        return (self.lower + self.upper) / 2;
    }

    fn area(self) -> Fixed {
        return (self.upper.x - self.lower.x) * (self.upper.y - self.lower.y);
    }

    fn perimeter(self) -> Fixed {
        return ((self.upper.x - self.lower.x) + (self.upper.y - self.lower.y)) * 2;
    }

    // Grow to include a point.
    fn include_point(ref self, p: Vec2) {
        self.lower = self.lower.componentwise_min(p);
        self.upper = self.upper.componentwise_max(p);
    }

    fn include_box(ref self, other: AABB) {
        self.lower = self.lower.componentwise_min(other.lower);
        self.upper = self.upper.componentwise_max(other.upper);
    }

    // Grow outward on every side by margin. The broad phase fattens every
    // proxy so that a body has to move a real distance before its cell
    // membership changes.
    fn expanded(self, margin: Fixed) -> AABB {
        return make_aabb(make_vec2(self.lower.x - margin, self.lower.y - margin),
                         make_vec2(self.upper.x + margin, self.upper.y + margin));
    }

    fn contains_point(self, p: Vec2) -> bool {
        return p.x >= self.lower.x and p.x <= self.upper.x and p.y >= self.lower.y and p.y <= self.upper.y;
    }

    fn contains_box(self, other: AABB) -> bool {
        return self.lower.x <= other.lower.x and self.lower.y <= other.lower.y
            and self.upper.x >= other.upper.x and self.upper.y >= other.upper.y;
    }

    // The separating-axis test in its simplest form: two boxes overlap unless
    // one is entirely to one side of the other on some axis.
    fn overlaps(self, other: AABB) -> bool {
        if (self.upper.x < other.lower.x or other.upper.x < self.lower.x) {
            return false;
        }
        if (self.upper.y < other.lower.y or other.upper.y < self.lower.y) {
            return false;
        }
        return true;
    }

    // Nearest point inside the box to p, which is p itself when p is inside.
    fn closest_point(self, p: Vec2) -> Vec2 {
        return make_vec2(p.x.clamped(self.lower.x, self.upper.x), p.y.clamped(self.lower.y, self.upper.y));
    }

    fn distance_squared_to_point(self, p: Vec2) -> Fixed {
        let nearest = self.closest_point(p);
        return (p - nearest).length_squared();
    }

    fn translated(self, offset: Vec2) -> AABB {
        return make_aabb(self.lower + offset, self.upper + offset);
    }

    // Sweep the box along a displacement, producing the box that covers the
    // whole path. Continuous collision detection uses this to decide whether
    // a fast body needs substepping of its own.
    fn swept(self, displacement: Vec2) -> AABB {
        let moved = self.translated(displacement);
        return make_aabb(self.lower.componentwise_min(moved.lower), self.upper.componentwise_max(moved.upper));
    }

    var lower: Vec2;
    var upper: Vec2;
}

fn make_aabb(lower: Vec2, upper: Vec2) -> AABB {
    var result: AABB;
    result.lower = lower + vec2_zero();
    result.upper = upper + vec2_zero();
    return result;
}

fn aabb_empty() -> AABB {
    var result: AABB;
    result.set_empty();
    return result;
}

fn aabb_from_centre(centre: Vec2, half: Vec2) -> AABB {
    return make_aabb(centre - half, centre + half);
}

// Union of two boxes, spelled with + so that folding a list of proxies into
// one bound is a single accumulation.
impl AABB as Core.AddWith(AABB) where .Result = AABB {
  fn Op(self, other: AABB) -> AABB {
      return make_aabb(self.lower.componentwise_min(other.lower), self.upper.componentwise_max(other.upper));
  }
}

// Translation of a box by a vector.
impl AABB as Core.AddWith(Vec2) where .Result = AABB {
  fn Op(self, other: Vec2) -> AABB {
      return make_aabb(self.lower + other, self.upper + other);
  }
}
impl AABB as Core.SubWith(Vec2) where .Result = AABB {
  fn Op(self, other: Vec2) -> AABB {
      return make_aabb(self.lower - other, self.upper - other);
  }
}

// Intersection. May produce an empty box, which is exactly the answer when
// the two do not overlap.
impl AABB as Core.MulWith(AABB) where .Result = AABB {
  fn Op(self, other: AABB) -> AABB {
      return make_aabb(self.lower.componentwise_max(other.lower), self.upper.componentwise_min(other.upper));
  }
}
impl AABB as Core.EqWith(AABB) {
  fn Equal(self, other: AABB) -> bool {
      return self.lower == other.lower and self.upper == other.upper;
  }

  fn NotEqual(self, other: AABB) -> bool {
      return self.lower != other.lower or self.upper != other.upper;
  }
}

// Ordering by area, so that a sorted proxy list puts the cheap tests first.
impl AABB as Core.OrderedWith(AABB) {
  fn Less(self, other: AABB) -> bool {
      return self.area() < other.area();
  }

  fn Greater(self, other: AABB) -> bool {
      return self.area() > other.area();
  }
}

// --- Circle ---------------------------------------------------------------

class Circle {

    fn area(self) -> Fixed {
        return pi_fixed() * self.radius * self.radius;
    }

    // Second moment of area about the centre, times the density, gives the
    // rotational inertia of a disc: m r^2 / 2. The offset term comes from the
    // parallel axis theorem when the circle is not at the body origin.
    fn inertia_for_mass(self, mass: Fixed) -> Fixed {
        let own = mass * self.radius * self.radius / 2;
        return own + mass * self.centre.length_squared();
    }

    fn bounds(self, frame: Transform) -> AABB {
        let world_centre = frame.apply(self.centre);
        let extent = make_vec2(self.radius, self.radius);
        return make_aabb(world_centre - extent, world_centre + extent);
    }

    fn contains_point(self, frame: Transform, p: Vec2) -> bool {
        let local = frame.unapply(p);
        return (local - self.centre).length_squared() <= self.radius * self.radius;
    }

    // The point on the circle furthest along a world-space direction. This is
    // the support function that the SAT code and the distance query share.
    fn support(self, frame: Transform, direction: Vec2) -> Vec2 {
        let unit = direction.normalised();
        return frame.apply(self.centre) + unit * self.radius;
    }

    var centre: Vec2;
    var radius: Fixed;
}

fn make_circle(centre: Vec2, radius: Fixed) -> Circle {
    var result: Circle;
    result.centre = centre + vec2_zero();
    result.radius = radius + zero_fixed();
    return result;
}

impl Circle as Core.EqWith(Circle) {
  fn Equal(self, other: Circle) -> bool {
      return self.centre == other.centre and self.radius == other.radius;
  }
}

// Scaling a circle scales both its offset and its radius, which is what a
// uniform scale of the whole body would do.
impl Circle as Core.MulWith(Fixed) where .Result = Circle {
  fn Op(self, other: Fixed) -> Circle {
      return make_circle(self.centre * other, self.radius * other);
  }
}

// --- Convex polygon -------------------------------------------------------

// Above this the polygon builder refuses the shape rather than silently
// dropping vertices.
let MAX_POLYGON_VERTICES: int = 16;

// A convex polygon in counter-clockwise winding, stored in body-local space
// together with its outward edge normals. normals[i] is the outward normal of
// the edge from vertices[i] to vertices[i + 1].
class Polygon {

    fn vertex_count(self) -> int {
        return self.vertices.size();
    }

    // Wrapping index access, so that the edge loops never need a modulo at
    // the call site.
    fn vertex(self, index: int) -> Vec2 {
        let count = self.vertices.size();
        if (count == 0) {
            return vec2_zero();
        }
        var wrapped = index % count;
        if (wrapped < 0) {
            wrapped += count;
        }
        return self.vertices[wrapped] + vec2_zero();
    }

    fn normal(self, index: int) -> Vec2 {
        let count = self.normals.size();
        if (count == 0) {
            return vec2_zero();
        }
        var wrapped = index % count;
        if (wrapped < 0) {
            wrapped += count;
        }
        return self.normals[wrapped] + vec2_zero();
    }

    fn world_vertex(self, frame: Transform, index: int) -> Vec2 {
        return frame.apply(self.vertex(index));
    }

    fn world_normal(self, frame: Transform, index: int) -> Vec2 {
        return frame.apply_direction(self.normal(index));
    }

    // Recompute the outward normals from the vertices. Counter-clockwise
    // winding means the outward normal of an edge is the edge rotated by -90
    // degrees, so (dx, dy) becomes (dy, -dx).
    fn rebuild_normals(ref self) {
        self.normals.clear();
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            let current = self.vertices[index];
            let next = self.vertices[(index + 1) % count];
            let edge = next - current;
            self.normals.push_back(edge.perpendicular_cw().normalised());
            index += 1;
        }
    }

    // Twice the signed area, by the shoelace formula. Positive means the
    // winding is counter-clockwise, which every routine below assumes.
    fn signed_area_twice(self) -> Fixed {
        var total = zero_fixed();
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            let current = self.vertices[index];
            let next = self.vertices[(index + 1) % count];
            total = total + current.x * next.y - next.x * current.y;
            index += 1;
        }
        return total;
    }

    fn area(self) -> Fixed {
        return self.signed_area_twice().magnitude() / 2;
    }

    // Area centroid, as the area-weighted mean of the fan triangles from the
    // origin. Each triangle contributes its own centroid, which is one third
    // of the sum of its corners, weighted by twice its signed area.
    fn centroid(self) -> Vec2 {
        var accumulated = vec2_zero();
        var total_area = zero_fixed();
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            let current = self.vertices[index];
            let next = self.vertices[(index + 1) % count];
            let cross_term = current.x * next.y - next.x * current.y;
            accumulated = accumulated + (current + next) * cross_term;
            total_area = total_area + cross_term;
            index += 1;
        }
        if (total_area.magnitude().raw <= LINEAR_EPSILON_RAW) {
            return vec2_zero();
        }
        return accumulated / (total_area * 3);
    }

    // Second moment of area about the origin, by the standard polygon
    // formula. Multiplying by density gives the rotational inertia.
    fn second_moment(self) -> Fixed {
        var numerator = zero_fixed();
        var denominator = zero_fixed();
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            let current = self.vertices[index];
            let next = self.vertices[(index + 1) % count];
            let cross_term = (current.x * next.y - next.x * current.y).magnitude();
            let spread = current.dot(current) + current.dot(next) + next.dot(next);
            numerator = numerator + cross_term * spread;
            denominator = denominator + cross_term;
            index += 1;
        }
        if (denominator.raw == 0) {
            return zero_fixed();
        }
        return numerator / (denominator * 6) * denominator;
    }

    fn inertia_for_mass(self, mass: Fixed) -> Fixed {
        let a = self.area();
        if (a.raw <= 0) {
            return zero_fixed();
        }
        return self.second_moment() * mass / a;
    }

    // The vertex furthest along a local-space direction, returned as an
    // index so that the SAT code can also reach its neighbours.
    fn support_index(self, direction: Vec2) -> int {
        let count = self.vertices.size();
        if (count == 0) {
            return 0;
        }
        var best: int = 0;
        var best_projection = self.vertices[0].dot(direction);
        var index: int = 1;
        while (index < count) {
            let projection = self.vertices[index].dot(direction);
            if (projection > best_projection) {
                best_projection = projection;
                best = index;
            }
            index += 1;
        }
        return best;
    }

    fn support(self, frame: Transform, direction: Vec2) -> Vec2 {
        let local_direction = frame.unapply_direction(direction);
        return frame.apply(self.vertex(self.support_index(local_direction)));
    }

    // Interval of the polygon's projection on a local-space axis.
    fn project_onto(self, axis: Vec2) -> Vec2 {
        let count = self.vertices.size();
        if (count == 0) {
            return vec2_zero();
        }
        var low = self.vertices[0].dot(axis);
        var high = low + zero_fixed();
        var index: int = 1;
        while (index < count) {
            let projection = self.vertices[index].dot(axis);
            low = fixed_min(low, projection);
            high = fixed_max(high, projection);
            index += 1;
        }
        return make_vec2(low, high);
    }

    fn bounds(self, frame: Transform) -> AABB {
        var box = aabb_empty();
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            box.include_point(frame.apply(self.vertices[index]));
            index += 1;
        }
        return box;
    }

    // A point is inside a convex counter-clockwise polygon exactly when it is
    // behind every edge plane.
    fn contains_point(self, frame: Transform, p: Vec2) -> bool {
        let local = frame.unapply(p);
        let count = self.vertices.size();
        var index: int = 0;
        while (index < count) {
            let offset = local - self.vertices[index];
            if (self.normals[index].dot(offset) > zero_fixed()) {
                return false;
            }
            index += 1;
        }
        return true;
    }

    // Convexity check, used only by the builder and the debug validator. A
    // convex counter-clockwise polygon turns left at every vertex.
    fn is_convex(self) -> bool {
        let count = self.vertices.size();
        if (count < 3) {
            return false;
        }
        var index: int = 0;
        while (index < count) {
            let a = self.vertex(index);
            let b = self.vertex(index + 1);
            let c = self.vertex(index + 2);
            if (triangle_area_twice(a, b, c) < zero_fixed()) {
                return false;
            }
            index += 1;
        }
        return true;
    }

    // Shift every vertex so that the centroid lands on the origin, which is
    // what lets the body treat its own origin as its centre of mass.
    fn recentre(ref self) {
        let offset = self.centroid();
        var index: int = 0;
        while (index < self.vertices.size()) {
            self.vertices[index] = self.vertices[index] - offset;
            index += 1;
        }
        self.rebuild_normals();
    }

    var vertices: vector(Vec2);
    var normals: vector(Vec2);
    var radius: Fixed;
}

// Build a polygon from a vertex list, rebuilding the normals and pushing the
// centroid to the origin. The caller is expected to supply a convex
// counter-clockwise loop; is_convex reports when it did not.
fn make_polygon(vertices: vector(Vec2)) -> Polygon {
    var result: Polygon;
    result.vertices = move(vertices);
    result.radius = raw_fixed(LINEAR_EPSILON_RAW);
    result.rebuild_normals();
    return result;
}

// The axis-aligned box of half width hx and half height hy, centred on the
// body origin, in counter-clockwise winding starting from the lower left.
fn make_box_polygon(hx: Fixed, hy: Fixed) -> Polygon {
    var corners: vector(Vec2);
    corners.push_back(make_vec2(-hx, -hy));
    corners.push_back(make_vec2(hx, -hy));
    corners.push_back(make_vec2(hx, hy));
    corners.push_back(make_vec2(-hx, hy));
    return make_polygon(move(corners));
}

// A regular n-gon of the given circumradius, with the first vertex on the
// positive x axis.
fn make_regular_polygon(sides: int, circumradius: Fixed) -> Polygon {
    var corners: vector(Vec2);
    var index: int = 0;
    while (index < sides and index < MAX_POLYGON_VERTICES) {
        let angle = tau_fixed() * index / sides;
        corners.push_back(vec2_from_angle(angle) * circumradius);
        index += 1;
    }
    return make_polygon(move(corners));
}

impl Polygon as Core.EqWith(Polygon) {
  fn Equal(self, other: Polygon) -> bool {
      if (self.vertices.size() != other.vertices.size()) {
          return false;
      }
      var index: int = 0;
      while (index < self.vertices.size()) {
          if (self.vertices[index] != other.vertices[index]) {
              return false;
          }
          index += 1;
      }
      return true;
  }
}

// --- Shape ----------------------------------------------------------------
//
// One of each payload plus a tag. A real implementation would use a sum type;
// this shape is a struct with both payloads because the language has none,
// and the unused payload costs four Fixed values.
class Shape {

    fn is_circle(self) -> bool {
        return self.kind == ShapeKind.Circle;
    }

    fn is_polygon(self) -> bool {
        return self.kind == ShapeKind.Polygon or self.kind == ShapeKind.Box;
    }

    fn area(self) -> Fixed {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.area();
        }
        return self.polygon.area();
    }

    fn mass_for_density(self, density: Fixed) -> Fixed {
        return self.area() * density;
    }

    fn inertia_for_mass(self, mass: Fixed) -> Fixed {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.inertia_for_mass(mass);
        }
        return self.polygon.inertia_for_mass(mass);
    }

    fn centroid(self) -> Vec2 {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.centre + vec2_zero();
        }
        return self.polygon.centroid();
    }

    fn bounds(self, frame: Transform) -> AABB {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.bounds(frame);
        }
        return self.polygon.bounds(frame);
    }

    fn contains_point(self, frame: Transform, p: Vec2) -> bool {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.contains_point(frame, p);
        }
        return self.polygon.contains_point(frame, p);
    }

    fn support(self, frame: Transform, direction: Vec2) -> Vec2 {
        if (self.kind == ShapeKind.Circle) {
            return self.circle.support(frame, direction);
        }
        return self.polygon.support(frame, direction);
    }

    var kind: ShapeKind = ShapeKind.Circle;
    var circle: Circle;
    var polygon: Polygon;
}

fn shape_from_circle(circle: Circle) -> Shape {
    var result: Shape;
    result.kind = ShapeKind.Circle;
    result.circle = move(circle);
    return result;
}

fn shape_from_polygon(polygon: Polygon) -> Shape {
    var result: Shape;
    result.kind = ShapeKind.Polygon;
    result.polygon = move(polygon);
    return result;
}

fn shape_box(hx: Fixed, hy: Fixed) -> Shape {
    var result = shape_from_polygon(make_box_polygon(hx, hy));
    result.kind = ShapeKind.Box;
    return result;
}

// --- Material -------------------------------------------------------------
//
// Density, restitution and the two friction coefficients. Restitution is the
// fraction of approach speed returned as separation speed: 0 is a dead stop,
// FIXED_ONE is a perfectly elastic bounce.
class Material {

    fn is_bouncy(self) -> bool {
        return self.restitution.raw > FIXED_QUARTER;
    }

    fn is_slippery(self) -> bool {
        return self.dynamic_friction.raw < raw_fixed(6553).raw;
    }

    var density: Fixed;
    var restitution: Fixed;
    var static_friction: Fixed;
    var dynamic_friction: Fixed;
}

fn make_material(density: Fixed, restitution: Fixed,
                 static_friction: Fixed, dynamic_friction: Fixed) -> Material {
    var result: Material;
    result.density = density + zero_fixed();
    result.restitution = restitution + zero_fixed();
    result.static_friction = static_friction + zero_fixed();
    result.dynamic_friction = dynamic_friction + zero_fixed();
    return result;
}

// Standard mixing rules. Restitution takes the larger of the pair, because a
// bouncy ball on dead concrete should still bounce; friction takes the
// geometric mean, which is the usual compromise.
fn mix_restitution(a: Material, b: Material) -> Fixed {
    return fixed_max(a.restitution, b.restitution);
}

fn mix_static_friction(a: Material, b: Material) -> Fixed {
    return (a.static_friction * b.static_friction).square_root();
}

fn mix_dynamic_friction(a: Material, b: Material) -> Fixed {
    return (a.dynamic_friction * b.dynamic_friction).square_root();
}

// A few presets so that the scene builder does not have to spell out four
// coefficients for every body. Values are raw fixed-point:
//   0.2 * 65536 = 13107, 0.3 * 65536 = 19660, 0.4 * 65536 = 26214,
//   0.6 * 65536 = 39321, 0.9 * 65536 = 58982.
fn material_wood() -> Material {
    return make_material(ratio(600, 1000), raw_fixed(19660), raw_fixed(39321), raw_fixed(26214));
}

fn material_steel() -> Material {
    return make_material(whole(8), raw_fixed(13107), raw_fixed(26214), raw_fixed(13107));
}

fn material_rubber() -> Material {
    return make_material(ratio(1200, 1000), raw_fixed(58982), raw_fixed(58982), raw_fixed(39321));
}

fn material_ice() -> Material {
    return make_material(ratio(900, 1000), raw_fixed(6553), raw_fixed(6553), raw_fixed(3276));
}

// --- Rigid body -----------------------------------------------------------
//
// The body owns its shape, its frame and its momentum state. Mass and inertia
// are stored both directly and as reciprocals, because the solver only ever
// wants the reciprocals and a static body is spelled by an inverse mass of
// zero rather than by a branch.
class RigidBody {

    // --- Mass properties --------------------------------------------------

    fn is_static(self) -> bool {
        return self.kind == BodyKind.Static;
    }

    fn is_dynamic(self) -> bool {
        return self.kind == BodyKind.Dynamic;
    }

    fn is_movable(self) -> bool {
        return self.kind != BodyKind.Static and self.inverse_mass.raw > 0;
    }

    // Recompute mass, centre of mass and inertia from the shape and the
    // material. A static or kinematic body gets zero inverse mass, which is
    // what makes every impulse applied to it a no-op.
    fn recompute_mass(ref self) {
        if (self.kind != BodyKind.Dynamic) {
            self.mass = zero_fixed();
            self.inverse_mass = zero_fixed();
            self.inertia = zero_fixed();
            self.inverse_inertia = zero_fixed();
            return;
        }
        self.mass = self.shape.mass_for_density(self.material.density);
        if (self.mass.raw <= 0) {
            self.mass = one_fixed();
        }
        self.inverse_mass = self.mass.reciprocal();
        self.inertia = self.shape.inertia_for_mass(self.mass);
        if (self.inertia.raw <= 0) {
            self.inverse_inertia = zero_fixed();
        } else {
            self.inverse_inertia = self.inertia.reciprocal();
        }
        self.local_centre = self.shape.centroid();
    }

    // Force a body to behave as if it were nailed down, without discarding
    // its stored mass, so that it can be released again later.
    fn freeze(ref self) {
        self.inverse_mass = zero_fixed();
        self.inverse_inertia = zero_fixed();
        self.velocity = vec2_zero();
        self.angular_velocity = zero_fixed();
    }

    fn thaw(ref self) {
        if (self.mass.raw > 0) {
            self.inverse_mass = self.mass.reciprocal();
        }
        if (self.inertia.raw > 0) {
            self.inverse_inertia = self.inertia.reciprocal();
        }
    }

    // --- Frame ------------------------------------------------------------

    fn position(self) -> Vec2 {
        return self.frame.position + vec2_zero();
    }

    fn angle(self) -> Fixed {
        return self.frame.angle + zero_fixed();
    }

    // Centre of mass in world space, which is where every impulse pivots.
    fn world_centre(self) -> Vec2 {
        return self.frame.position + self.frame.rotation * self.local_centre;
    }

    fn set_position(ref self, p: Vec2) {
        self.frame.position = p + vec2_zero();
        self.bounds_dirty = true;
    }

    fn set_angle(ref self, angle: Fixed) {
        self.frame.set_angle(angle);
        self.bounds_dirty = true;
    }

    fn to_world(self, local: Vec2) -> Vec2 {
        return self.frame.apply(local);
    }

    fn to_local(self, world: Vec2) -> Vec2 {
        return self.frame.unapply(world);
    }

    // --- Velocity ---------------------------------------------------------

    // Velocity of the material point currently at the given world position.
    // The angular term is omega cross r, which in 2D is a 90 degree rotation
    // of the offset scaled by the angular speed.
    fn velocity_at(self, world_point: Vec2) -> Vec2 {
        let offset = world_point - self.world_centre();
        return make_vec2(self.velocity.x - self.angular_velocity * offset.y,
                         self.velocity.y + self.angular_velocity * offset.x);
    }

    fn set_velocity(ref self, v: Vec2) {
        self.velocity = v + vec2_zero();
        self.sleep_timer = zero_fixed();
        self.awake = true;
    }

    fn add_velocity(ref self, delta: Vec2) {
        self.velocity = self.velocity + delta;
        self.awake = true;
    }

    // --- Forces and impulses ----------------------------------------------

    fn clear_forces(ref self) {
        self.force = vec2_zero();
        self.torque = zero_fixed();
    }

    fn apply_force(ref self, f: Vec2) {
        self.force = self.force + f;
        self.awake = true;
    }

    // A force applied off the centre of mass also produces a torque equal to
    // the cross product of the lever arm with the force.
    fn apply_force_at(ref self, f: Vec2, world_point: Vec2) {
        let offset = world_point - self.world_centre();
        self.force = self.force + f;
        self.torque = self.torque + offset.x * f.y - offset.y * f.x;
        self.awake = true;
    }

    fn apply_torque(ref self, t: Fixed) {
        self.torque = self.torque + t;
        self.awake = true;
    }

    // The two workhorses of the solver. An impulse changes velocity directly
    // rather than through an integration step, so it is scaled by the inverse
    // mass and not by the timestep.
    fn apply_impulse(ref self, impulse: Vec2) {
        self.velocity = self.velocity + impulse * self.inverse_mass;
    }

    fn apply_impulse_at(ref self, impulse: Vec2, world_point: Vec2) {
        let offset = world_point - self.world_centre();
        self.velocity.x = self.velocity.x + impulse.x * self.inverse_mass;
        self.velocity.y = self.velocity.y + impulse.y * self.inverse_mass;
        self.angular_velocity = self.angular_velocity
            + self.inverse_inertia * (offset.x * impulse.y - offset.y * impulse.x);
    }

    fn apply_angular_impulse(ref self, amount: Fixed) {
        self.angular_velocity = self.angular_velocity + amount * self.inverse_inertia;
    }

    // --- Integration ------------------------------------------------------

    // Semi-implicit Euler on the velocity. Gravity is added as an
    // acceleration rather than as a force so that a body's mass does not
    // change how fast it falls, and damping is applied as a multiplicative
    // decay so that it is stable at any timestep.
    fn integrate_velocity(ref self, gravity: Vec2, dt: Fixed) {
        if (self.inverse_mass.raw == 0) {
            return;
        }
        self.velocity = self.velocity
            + (gravity * self.gravity_scale + self.force * self.inverse_mass) * dt;
        self.angular_velocity = self.angular_velocity + self.torque * self.inverse_inertia * dt;
        self.velocity = self.velocity * (one_fixed() / (one_fixed() + self.linear_damping * dt));
        self.angular_velocity = self.angular_velocity
            / (one_fixed() + self.angular_damping * dt);
        self.velocity = self.velocity.clamped_length(self.max_linear_speed);
        self.angular_velocity = self.angular_velocity.clamped(-self.max_angular_speed, self.max_angular_speed);
    }

    fn integrate_position(ref self, dt: Fixed) {
        if (self.kind == BodyKind.Static) {
            return;
        }
        self.frame.position = self.frame.position + self.velocity * dt;
        self.frame.angle = self.frame.angle + self.angular_velocity * dt;
        self.frame.refresh();
        self.bounds_dirty = true;
    }

    // --- Energy -----------------------------------------------------------

    // Translational plus rotational kinetic energy: m v^2 / 2 + I w^2 / 2.
    fn kinetic_energy(self) -> Fixed {
        let linear = self.mass * self.velocity.length_squared() / 2;
        let angular = self.inertia * self.angular_velocity * self.angular_velocity / 2;
        return linear + angular;
    }

    // Potential energy relative to a datum height, measured against the
    // world's gravity direction.
    fn potential_energy(self, gravity: Vec2, datum: Fixed) -> Fixed {
        return -self.mass * (gravity.x * self.world_centre().x
                       + gravity.y * (self.world_centre().y - datum));
    }

    fn linear_momentum(self) -> Vec2 {
        return self.velocity * self.mass;
    }

    fn angular_momentum_about(self, pivot: Vec2) -> Fixed {
        let offset = self.world_centre() - pivot;
        return self.inertia * self.angular_velocity
            + self.mass * (offset.x * self.velocity.y - offset.y * self.velocity.x);
    }

    // --- Sleeping ---------------------------------------------------------

    // A body that has been slow for long enough stops being simulated. The
    // thresholds are squared so that the test avoids a square root.
    fn update_sleep(ref self, dt: Fixed, linear_threshold: Fixed,
                    angular_threshold: Fixed, time_to_sleep: Fixed) {
        if (self.kind == BodyKind.Static) {
            return;
        }
        if (self.never_sleeps) {
            self.sleep_timer = zero_fixed();
            return;
        }
        let moving = self.velocity.length_squared() > linear_threshold * linear_threshold
            or self.angular_velocity.magnitude() > angular_threshold;
        if (moving) {
            self.sleep_timer = zero_fixed();
            self.awake = true;
        } else {
            self.sleep_timer = self.sleep_timer + dt;
            if (self.sleep_timer >= time_to_sleep) {
                self.awake = false;
                self.velocity = vec2_zero();
                self.angular_velocity = zero_fixed();
            }
        }
    }

    fn wake(ref self) {
        self.awake = true;
        self.sleep_timer = zero_fixed();
    }

    // --- Bounds -----------------------------------------------------------

    fn bounds(self) -> AABB {
        return self.shape.bounds(self.frame);
    }

    // The proxy box the broad phase indexes: the tight bounds, fattened by a
    // margin and swept along the current velocity so that a body cannot
    // tunnel out of its own cell within one step.
    fn proxy_bounds(self, margin: Fixed, dt: Fixed) -> AABB {
        return self.shape.bounds(self.frame).swept(self.velocity * dt).expanded(margin);
    }

    // --- Identity ---------------------------------------------------------

    var kind: BodyKind = BodyKind.Dynamic;
    var identifier: int = 0;
    var layer: int = 1;
    var mask: int = 65535;

    var shape: Shape;
    var material: Material;
    var frame: Transform;
    var local_centre: Vec2;

    var mass: Fixed;
    var inverse_mass: Fixed;
    var inertia: Fixed;
    var inverse_inertia: Fixed;

    var velocity: Vec2;
    var angular_velocity: Fixed;
    var force: Vec2;
    var torque: Fixed;

    var linear_damping: Fixed;
    var angular_damping: Fixed;
    var gravity_scale: Fixed;
    var max_linear_speed: Fixed;
    var max_angular_speed: Fixed;

    var sleep_timer: Fixed;
    var awake: bool = true;
    var never_sleeps: bool = false;
    var bounds_dirty: bool = true;
}

// Two bodies collide only when their layer and mask bits agree in both
// directions, and never when both are immovable.
fn should_collide(a: RigidBody, b: RigidBody) -> bool {
    if (a.inverse_mass.raw == 0 and b.inverse_mass.raw == 0) {
        return false;
    }
    if (not a.awake and not b.awake) {
        return false;
    }
    return (a.layer & b.mask) != 0 and (b.layer & a.mask) != 0;
}

// Ordering by identifier, so that the contact list is deterministic no matter
// what order the broad phase happened to visit its cells in.
impl RigidBody as Core.OrderedWith(RigidBody) {
  fn Less(self, other: RigidBody) -> bool {
      return self.identifier < other.identifier;
  }
}
impl RigidBody as Core.EqWith(RigidBody) {
  fn Equal(self, other: RigidBody) -> bool {
      return self.identifier == other.identifier;
  }
}

// --- Broad phase: uniform spatial hash ------------------------------------
//
// Every proxy box is rasterised into the cells of an infinite uniform grid,
// and each cell index is folded into a bucket by a pair of large primes. The
// grid is infinite because the hash wraps; the cost is that two distant cells
// can share a bucket, which the pair filter removes at the end.
//
// The cell size wants to be about twice the average proxy diameter: too small
// and a body lands in many cells, too large and every cell holds everything.

// Multiplicative hash constants. Both are primes far from any power of two.
let HASH_PRIME_X: int = 73856093;
let HASH_PRIME_Y: int = 19349663;
let HASH_PRIME_MIX: int = 83492791;

// Default bucket count. A power of two so that the fold to a bucket index is
// a mask rather than a remainder.
let DEFAULT_BUCKET_COUNT: int = 4096;
let DEFAULT_BUCKET_MASK: int = 4095;

// A body whose proxy covers more cells than this is put on the oversized list
// and tested against everything instead of being rasterised.
let MAX_CELLS_PER_PROXY: int = 64;

// One entry in a bucket: which body, and which cell it was hashed from. The
// cell is kept so that a query can reject the far half of a collision without
// touching the body.
class GridEntry {
    var body_index: int = 0;
    var cell_x: int = 0;
    var cell_y: int = 0;
    var generation: int = 0;
}

// An unordered pair of body indices, normalised so that low is always the
// smaller. Normalising at construction is what makes the duplicate filter a
// plain equality test.
class BodyPair {

    fn normalise(ref self) {
        if (self.low > self.high) {
            swap(self.low, self.high);
        }
    }

    fn key(self) -> int {
        return self.low * 1000003 + self.high;
    }

    var low: int = 0;
    var high: int = 0;
}

fn make_pair(a: int, b: int) -> BodyPair {
    var result: BodyPair;
    if (a < b) {
        result.low = a;
        result.high = b;
    } else {
        result.low = b;
        result.high = a;
    }
    return result;
}

impl BodyPair as Core.EqWith(BodyPair) {
  fn Equal(self, other: BodyPair) -> bool {
      return self.low == other.low and self.high == other.high;
  }

  fn NotEqual(self, other: BodyPair) -> bool {
      return self.low != other.low or self.high != other.high;
  }
}

// Lexicographic order on (low, high), which is what the duplicate sweep wants.
impl BodyPair as Core.OrderedWith(BodyPair) {
  fn Less(self, other: BodyPair) -> bool {
      if (self.low != other.low) {
          return self.low < other.low;
      }
      return self.high < other.high;
  }

  fn Greater(self, other: BodyPair) -> bool {
      if (self.low != other.low) {
          return self.low > other.low;
      }
      return self.high > other.high;
  }
}

class SpatialHash {

    // --- Configuration ----------------------------------------------------

    fn configure(ref self, cell_size: Fixed, buckets: int) {
        self.cell_size = cell_size + zero_fixed();
        self.inverse_cell_size = cell_size.reciprocal();
        self.bucket_count = buckets;
        self.bucket_mask = buckets - 1;
        self.buckets.clear();
        self.buckets.resize(buckets);
        self.generation = 0;
    }

    // --- Cell arithmetic --------------------------------------------------

    // World coordinate to cell coordinate, flooring so that the cell boundary
    // at zero does not get counted twice.
    fn cell_of(self, value: Fixed) -> int {
        return (value * self.inverse_cell_size).floor_int();
    }

    fn cell_x_of_point(self, p: Vec2) -> int {
        return (p.x * self.inverse_cell_size).floor_int();
    }

    fn cell_y_of_point(self, p: Vec2) -> int {
        return (p.y * self.inverse_cell_size).floor_int();
    }

    // Fold a cell coordinate pair into a bucket. The multiply-and-xor is the
    // classic Teschner spatial hash; the final mask is valid only because the
    // bucket count is a power of two.
    fn bucket_of(self, cx: int, cy: int) -> int {
        let mixed = (cx * HASH_PRIME_X) ^ (cy * HASH_PRIME_Y) ^ HASH_PRIME_MIX;
        var index = mixed & self.bucket_mask;
        if (index < 0) {
            index += self.bucket_count;
        }
        return index;
    }

    // How many cells a box would occupy, used to decide whether a proxy goes
    // on the oversized list instead.
    fn cell_span(self, box: AABB) -> int {
        let low_x = self.cell_of(box.lower.x);
        let low_y = self.cell_of(box.lower.y);
        let high_x = self.cell_of(box.upper.x);
        let high_y = self.cell_of(box.upper.y);
        return (high_x - low_x + 1) * (high_y - low_y + 1);
    }

    // --- Population -------------------------------------------------------

    fn clear(ref self) {
        var index: int = 0;
        while (index < self.buckets.size()) {
            self.buckets[index].clear();
            index += 1;
        }
        self.oversized.clear();
        self.proxy_bounds.clear();
        self.entry_count = 0;
        self.generation += 1;
    }

    // Rasterise one proxy box into every cell it touches.
    fn insert(ref self, body_index: int, box: AABB) {
        self.proxy_bounds.push_back(box + vec2_zero());
        if (self.cell_span(box) > MAX_CELLS_PER_PROXY) {
            self.oversized.push_back(body_index);
            return;
        }
        let low_x = self.cell_of(box.lower.x);
        let low_y = self.cell_of(box.lower.y);
        let high_x = self.cell_of(box.upper.x);
        let high_y = self.cell_of(box.upper.y);
        var cy = low_y;
        while (cy <= high_y) {
            var cx = low_x;
            while (cx <= high_x) {
                var entry: GridEntry;
                entry.body_index = body_index;
                entry.cell_x = cx;
                entry.cell_y = cy;
                entry.generation = self.generation;
                self.buckets[self.bucket_of(cx, cy)].push_back(entry);
                self.entry_count += 1;
                cx += 1;
            }
            cy += 1;
        }
    }

    // Rebuild the whole grid from a body list. Rebuilding beats incremental
    // update for a scene where most bodies move every step, which is the case
    // this engine is tuned for.
    fn rebuild(ref self, bodies: vector(RigidBody),
               margin: Fixed, dt: Fixed) {
        self.clear();
        var index: int = 0;
        while (index < bodies.size()) {
            let ref body: auto = bodies[index];
            if (body.awake or body.kind == BodyKind.Static) {
                self.insert(index, body.proxy_bounds(margin, dt));
            } else {
                self.proxy_bounds.push_back(body.bounds());
            }
            index += 1;
        }
    }

    // --- Queries ----------------------------------------------------------

    // Every body whose proxy shares a cell with the query box. Duplicates are
    // possible when a body spans several of the queried cells; the caller
    // filters them.
    fn query_box(self, box: AABB, ref out: vector(int)) {
        let low_x = self.cell_of(box.lower.x);
        let low_y = self.cell_of(box.lower.y);
        let high_x = self.cell_of(box.upper.x);
        let high_y = self.cell_of(box.upper.y);
        var cy = low_y;
        while (cy <= high_y) {
            var cx = low_x;
            while (cx <= high_x) {
                let ref bucket: auto = self.buckets[self.bucket_of(cx, cy)];
                var slot: int = 0;
                while (slot < bucket.size()) {
                    let ref entry: auto = bucket[slot];
                    // Reject the far half of a hash collision by checking the
                    // cell coordinates the entry was actually filed under.
                    if (entry.cell_x == cx and entry.cell_y == cy) {
                        out.push_back(entry.body_index);
                    }
                    slot += 1;
                }
                cx += 1;
            }
            cy += 1;
        }
        var extra: int = 0;
        while (extra < self.oversized.size()) {
            out.push_back(self.oversized[extra]);
            extra += 1;
        }
    }

    fn query_point(self, p: Vec2, ref out: vector(int)) {
        self.query_box(make_aabb(p, p), out);
    }

    fn query_circle(self, centre: Vec2, radius: Fixed,
                    ref out: vector(int)) {
        let extent = make_vec2(radius, radius);
        self.query_box(make_aabb(centre - extent, centre + extent), out);
    }

    // --- Pair generation --------------------------------------------------

    // Collect every candidate pair by walking each bucket and emitting the
    // upper triangle of the entries that share a cell. Only entries whose
    // cell coordinates match are considered, so a hash collision costs a
    // comparison rather than a false pair.
    fn collect_pairs(self, bodies: vector(RigidBody),
                     ref out: vector(BodyPair)) {
        var bucket_index: int = 0;
        while (bucket_index < self.buckets.size()) {
            let ref bucket: auto = self.buckets[bucket_index];
            var first: int = 0;
            while (first < bucket.size()) {
                var second = first + 1;
                while (second < bucket.size()) {
                    let ref a: auto = bucket[first];
                    let ref b: auto = bucket[second];
                    if (a.cell_x == b.cell_x and a.cell_y == b.cell_y
                        and a.body_index != b.body_index) {
                        let ref left: auto = bodies[a.body_index];
                        let ref right: auto = bodies[b.body_index];
                        if (should_collide(left, right)
                            and self.proxy_bounds[a.body_index].overlaps(self.proxy_bounds[b.body_index])) {
                            out.push_back(make_pair(a.body_index, b.body_index));
                            broad_phase_pair_count += 1;
                        }
                    }
                    second += 1;
                }
                first += 1;
            }
            bucket_index += 1;
        }
        // Oversized proxies never made it into a bucket, so they are tested
        // against every body directly.
        var big: int = 0;
        while (big < self.oversized.size()) {
            let subject = self.oversized[big];
            var other: int = 0;
            while (other < bodies.size()) {
                if (other != subject) {
                    let ref left: auto = bodies[subject];
                    let ref right: auto = bodies[other];
                    if (should_collide(left, right)
                        and self.proxy_bounds[subject].overlaps(self.proxy_bounds[other])) {
                        out.push_back(make_pair(subject, other));
                        broad_phase_pair_count += 1;
                    }
                }
                other += 1;
            }
            big += 1;
        }
    }

    // --- Statistics -------------------------------------------------------

    fn occupied_buckets(self) -> int {
        var count: int = 0;
        var index: int = 0;
        while (index < self.buckets.size()) {
            if (self.buckets[index].size() > 0) {
                count += 1;
            }
            index += 1;
        }
        return count;
    }

    fn longest_bucket(self) -> int {
        var longest: int = 0;
        var index: int = 0;
        while (index < self.buckets.size()) {
            if (self.buckets[index].size() > longest) {
                longest = self.buckets[index].size();
            }
            index += 1;
        }
        return longest;
    }

    // Average entries per occupied bucket, in fixed point. A load much above
    // four says the cell size is too large for the scene.
    fn average_load(self) -> Fixed {
        let occupied = self.occupied_buckets();
        if (occupied == 0) {
            return zero_fixed();
        }
        return whole(self.entry_count) / whole(occupied);
    }

    var buckets: vector(vector(GridEntry));
    var oversized: vector(int);
    var proxy_bounds: vector(AABB);
    var cell_size: Fixed;
    var inverse_cell_size: Fixed;
    var bucket_count: int = 4096;
    var bucket_mask: int = 4095;
    var entry_count: int = 0;
    var generation: int = 0;
}

fn make_spatial_hash(cell_size: Fixed) -> SpatialHash {
    var result: SpatialHash;
    result.configure(cell_size, DEFAULT_BUCKET_COUNT);
    return result;
}

// Remove duplicate pairs from a list that has already been sorted. The list
// arrives sorted because make_pair normalises the order and the caller sorts
// on the lexicographic operator declared above.
fn deduplicate_pairs(ref pairs: vector(BodyPair)) {
    if (pairs.size() < 2) {
        return;
    }
    var write: int = 1;
    var read: int = 1;
    while (read < pairs.size()) {
        if (pairs[read] != pairs[write - 1]) {
            pairs[write] = pairs[read];
            write += 1;
        }
        read += 1;
    }
    while (pairs.size() > write) {
        pairs.pop_back();
    }
}

// Insertion sort on the pair list. The list is nearly sorted from one step to
// the next because the bucket walk order barely changes, so insertion sort is
// close to linear here and is stable, which keeps the simulation replayable.
fn sort_pairs(ref pairs: vector(BodyPair)) {
    var index: int = 1;
    while (index < pairs.size()) {
        var slot = index;
        while (slot > 0 and pairs[slot] < pairs[slot - 1]) {
            swap(pairs[slot], pairs[slot - 1]);
            slot -= 1;
        }
        index += 1;
    }
}

// --- Narrow phase ---------------------------------------------------------
//
// Every test in this section produces a Manifold: a shared normal pointing
// from A towards B, and up to two contact points, each with its own
// penetration depth and its own accumulated impulses.
//
// Two points is enough in 2D. A face-face contact between convex polygons
// clips to a segment, and a segment has two ends.

// The most contact points a single manifold can hold.
let MAX_MANIFOLD_POINTS: int = 2;

// Contacts closer than this are treated as the same point when matching a new
// manifold against the previous step's manifold for warm starting.
let CONTACT_MATCH_TOLERANCE_RAW: int = 655;

// Penetration below this is left alone: trying to remove the last fraction of
// a unit of overlap is what makes a stack jitter.
let PENETRATION_SLOP_RAW: int = 328;

// Relative normal speed below this is treated as a resting contact and gets no
// restitution, which is what stops a stack of boxes buzzing forever.
let RESTITUTION_THRESHOLD_RAW: int = 65536;

// A single point of contact.
class ContactPoint {

    // Recompute the two lever arms after the bodies have moved. The solver
    // does this once per substep rather than once per iteration.
    fn refresh_anchors(ref self, centre_a: Vec2, centre_b: Vec2) {
        self.anchor_a = self.position - centre_a;
        self.anchor_b = self.position - centre_b;
    }

    fn is_new(self) -> bool {
        return self.match_count == 0;
    }

    // World position of the contact.
    var position: Vec2;
    // Lever arm from each body's centre of mass to the contact.
    var anchor_a: Vec2;
    var anchor_b: Vec2;
    // Local-space anchors, kept so that position correction can recompute the
    // world positions after the bodies have been pushed apart.
    var local_a: Vec2;
    var local_b: Vec2;
    // Overlap along the manifold normal, positive when penetrating.
    var separation: Fixed;
    // Accumulated impulses, carried across steps for warm starting.
    var normal_impulse: Fixed;
    var tangent_impulse: Fixed;
    // Effective masses along the normal and the tangent, computed once per
    // substep because they only depend on the geometry.
    var normal_mass: Fixed;
    var tangent_mass: Fixed;
    // The target separation speed produced by restitution.
    var velocity_bias: Fixed;
    // Which feature pair produced this point, used to match across steps.
    var feature_id: int = 0;
    var match_count: int = 0;
}

fn make_contact_point(position: Vec2, separation: Fixed, feature: int) -> ContactPoint {
    var result: ContactPoint;
    result.position = position + vec2_zero();
    result.separation = separation + zero_fixed();
    result.feature_id = feature;
    return result;
}

// A contact manifold between two bodies.
class Manifold {

    fn point_count(self) -> int {
        return self.points.size();
    }

    fn is_touching(self) -> bool {
        return self.state != ContactState.Separated and self.points.size() > 0;
    }

    fn clear(ref self) {
        self.points.clear();
        self.state = ContactState.Separated;
    }

    // The unit tangent, which is the normal rotated by 90 degrees. Friction
    // acts along this direction.
    fn tangent(self) -> Vec2 {
        return make_vec2(-self.normal.y, self.normal.x);
    }

    // Deepest penetration across all points, which the position solver uses
    // to decide whether this manifold still needs work.
    fn deepest_separation(self) -> Fixed {
        var deepest = zero_fixed();
        var index: int = 0;
        while (index < self.points.size()) {
            deepest = fixed_min(deepest, self.points[index].separation);
            index += 1;
        }
        return deepest;
    }

    // Total normal impulse applied last step, which is what a breakable joint
    // or a sound trigger reads.
    fn total_normal_impulse(self) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.points.size()) {
            total = total + self.points[index].normal_impulse;
            index += 1;
        }
        return total;
    }

    fn centre_of_pressure(self) -> Vec2 {
        let total = self.total_normal_impulse();
        if (total.raw == 0) {
            if (self.points.size() == 0) {
                return vec2_zero();
            }
            return self.points[0].position + vec2_zero();
        }
        var weighted = vec2_zero();
        var index: int = 0;
        while (index < self.points.size()) {
            weighted = weighted + self.points[index].position * self.points[index].normal_impulse;
            index += 1;
        }
        return weighted / total;
    }

    // Carry the accumulated impulses from the previous step's manifold onto
    // the matching points of this one. Warm starting is the single change
    // that turns a jittery stack into a stable one, and it costs one pass
    // over at most four point pairs.
    fn warm_start_from(ref self, previous: Manifold) {
        var index: int = 0;
        while (index < self.points.size()) {
            var found: bool = false;
            var other: int = 0;
            while (other < previous.points.size() and not found) {
                let ref old: auto = previous.points[other];
                let same_feature = old.feature_id == self.points[index].feature_id;
                let close = (old.position - self.points[index].position).manhattan_length()
                    <= raw_fixed(CONTACT_MATCH_TOLERANCE_RAW);
                if (same_feature or close) {
                    self.points[index].normal_impulse = old.normal_impulse + zero_fixed();
                    self.points[index].tangent_impulse = old.tangent_impulse + zero_fixed();
                    self.points[index].match_count = old.match_count + 1;
                    found = true;
                }
                other += 1;
            }
            if (not found) {
                self.points[index].normal_impulse = zero_fixed();
                self.points[index].tangent_impulse = zero_fixed();
                self.points[index].match_count = 0;
            }
            index += 1;
        }
    }

    var body_a: int = 0;
    var body_b: int = 0;
    // Unit normal, pointing from body A towards body B.
    var normal: Vec2;
    var points: vector(ContactPoint);
    var state: ContactState = ContactState.Separated;
    var feature: FeatureKind = FeatureKind.FaceA;
    var friction: Fixed;
    var restitution: Fixed;
}

fn make_manifold(body_a: int, body_b: int) -> Manifold {
    var result: Manifold;
    result.body_a = body_a;
    result.body_b = body_b;
    result.normal = vec2_unit_y();
    return result;
}

// --- Circle against circle ------------------------------------------------
//
// The simplest test there is: compare the centre distance against the sum of
// the radii. The only subtlety is the coincident-centres case, where there is
// no direction to separate along and an arbitrary one has to be picked.
fn collide_circle_circle(ref manifold: Manifold,
                         circle_a: Circle, frame_a: Transform,
                         circle_b: Circle, frame_b: Transform) {
    narrow_phase_test_count += 1;
    manifold.clear();
    let centre_a = frame_a.apply(circle_a.centre);
    let centre_b = frame_b.apply(circle_b.centre);
    let offset = centre_b - centre_a;
    let distance_squared = offset.length_squared();
    let combined = circle_a.radius + circle_b.radius;
    if (distance_squared > combined * combined) {
        manifold.state = ContactState.Separated;
        return;
    }
    let distance = distance_squared.square_root();
    if (distance.raw <= LINEAR_EPSILON_RAW) {
        // Concentric circles: separate straight up, which at least keeps the
        // result deterministic.
        manifold.normal = vec2_unit_y();
        manifold.points.push_back(make_contact_point(centre_a, -combined, 0));
        manifold.state = ContactState.Penetrating;
        return;
    }
    manifold.normal = offset / distance;
    let surface_a = centre_a + manifold.normal * circle_a.radius;
    let surface_b = centre_b - manifold.normal * circle_b.radius;
    let midpoint = (surface_a + surface_b) / 2;
    manifold.points.push_back(make_contact_point(midpoint, distance - combined, 0));
    manifold.state = ContactState.Penetrating;
}

// --- Circle against convex polygon ----------------------------------------
//
// Work in the polygon's local frame so that the edge normals are already the
// right ones. Find the edge with the greatest signed separation; if that
// separation exceeds the radius there is no contact. Otherwise the contact is
// either against the face, when the circle centre projects inside the edge,
// or against one of the two endpoints.
fn collide_circle_polygon(ref manifold: Manifold,
                          circle: Circle, circle_frame: Transform,
                          polygon: Polygon, polygon_frame: Transform,
                          flip_normal: bool) {
    narrow_phase_test_count += 1;
    manifold.clear();
    let world_centre = circle_frame.apply(circle.centre);
    let local_centre = polygon_frame.unapply(world_centre);
    let count = polygon.vertices.size();
    if (count == 0) {
        manifold.state = ContactState.Separated;
        return;
    }

    var best_edge: int = 0;
    var best_separation = raw_fixed(FIXED_MIN_RAW);
    var index: int = 0;
    while (index < count) {
        let separation = polygon.normals[index].dot(local_centre - polygon.vertices[index]);
        if (separation > circle.radius) {
            manifold.state = ContactState.Separated;
            return;
        }
        if (separation > best_separation) {
            best_separation = separation;
            best_edge = index;
        }
        index += 1;
    }

    let vertex_a = polygon.vertex(best_edge);
    let vertex_b = polygon.vertex(best_edge + 1);

    // Deeply inside: the centre is behind every edge plane, so the face with
    // the least negative separation is the one to push out of.
    if (best_separation.raw < LINEAR_EPSILON_RAW and best_separation.raw <= 0) {
        let face_normal = polygon.normals[best_edge];
        let world_normal = polygon_frame.apply_direction(face_normal);
        let contact = world_centre - world_normal * circle.radius;
        if (flip_normal) {
            manifold.normal = -world_normal;
        } else {
            manifold.normal = world_normal + vec2_zero();
        }
        manifold.points.push_back(make_contact_point(contact, best_separation - circle.radius, best_edge));
        manifold.state = ContactState.Penetrating;
        return;
    }

    // Otherwise decide between the face region and the two vertex regions by
    // the sign of the projection onto the edge.
    let edge = vertex_b - vertex_a;
    let projection_a = (local_centre - vertex_a).dot(edge);
    let projection_b = (local_centre - vertex_b).dot(edge);
    var local_normal = vec2_zero();
    var local_contact = vec2_zero();
    var feature = best_edge;
    if (projection_a <= zero_fixed()) {
        if ((local_centre - vertex_a).length_squared() > circle.radius * circle.radius) {
            manifold.state = ContactState.Separated;
            return;
        }
        local_normal = (local_centre - vertex_a).normalised();
        local_contact = vertex_a + vec2_zero();
        feature = best_edge * 4 + 1;
    } else {
        if (projection_b >= zero_fixed()) {
            if ((local_centre - vertex_b).length_squared() > circle.radius * circle.radius) {
                manifold.state = ContactState.Separated;
                return;
            }
            local_normal = (local_centre - vertex_b).normalised();
            local_contact = vertex_b + vec2_zero();
            feature = best_edge * 4 + 2;
        } else {
            local_normal = polygon.normals[best_edge] + vec2_zero();
            local_contact = closest_point_on_segment(vertex_a, vertex_b, local_centre);
            feature = best_edge * 4;
        }
    }

    let world_normal = polygon_frame.apply_direction(local_normal);
    let world_contact = polygon_frame.apply(local_contact);
    let separation = (local_centre - local_contact).dot(local_normal) - circle.radius;
    if (flip_normal) {
        manifold.normal = -world_normal;
    } else {
        manifold.normal = world_normal + vec2_zero();
    }
    manifold.points.push_back(make_contact_point(world_contact, separation, feature));
    manifold.state = ContactState.Penetrating;
}

// --- Polygon against polygon: separating axis test ------------------------

// The result of scanning one polygon's faces against the other polygon.
class AxisQuery {
    var edge_index: int = 0;
    var separation: Fixed;
}

// Greatest separation of polygon b from any face plane of polygon a. A
// positive result means a separating axis exists and the shapes are apart.
//
// The support point is taken along the inward normal, which is the vertex of
// b that is deepest into a's face plane.
fn greatest_separation(a: Polygon, frame_a: Transform,
                       b: Polygon, frame_b: Transform) -> AxisQuery {
    var result: AxisQuery;
    result.separation = raw_fixed(FIXED_MIN_RAW);
    let count = a.vertices.size();
    var index: int = 0;
    while (index < count) {
        let world_normal = frame_a.apply_direction(a.normals[index]);
        let local_normal = frame_b.unapply_direction(world_normal);
        let support = b.vertex(b.support_index(-local_normal));
        let world_support = frame_b.apply(support);
        let world_vertex = frame_a.apply(a.vertices[index]);
        let separation = world_normal.dot(world_support - world_vertex);
        if (separation > result.separation) {
            result.separation = separation;
            result.edge_index = index;
        }
        index += 1;
    }
    return result;
}

// One end of a clipped edge, carrying the identifier of the feature pair that
// produced it so that warm starting can match it next step.
class ClipVertex {
    var position: Vec2;
    var identifier: int = 0;
}

fn make_clip_vertex(position: Vec2, identifier: int) -> ClipVertex {
    var result: ClipVertex;
    result.position = position + vec2_zero();
    result.identifier = identifier;
    return result;
}

// The edge of polygon b that is most anti-parallel to the reference normal,
// which is the edge that will be clipped against the reference face.
fn find_incident_edge(reference_normal: Vec2,
                      incident: Polygon, incident_frame: Transform,
                      ref out: vector(ClipVertex)) {
    out.clear();
    let local_normal = incident_frame.unapply_direction(reference_normal);
    let count = incident.normals.size();
    if (count == 0) {
        return;
    }
    var best: int = 0;
    var best_dot = incident.normals[0].dot(local_normal);
    var index: int = 1;
    while (index < count) {
        let value = incident.normals[index].dot(local_normal);
        if (value < best_dot) {
            best_dot = value;
            best = index;
        }
        index += 1;
    }
    out.push_back(make_clip_vertex(incident_frame.apply(incident.vertex(best)), best * 4));
    out.push_back(make_clip_vertex(incident_frame.apply(incident.vertex(best + 1)), best * 4 + 1));
}

// Clip a segment against a half plane, keeping the part with signed distance
// at most offset. Returns the number of surviving points, which is zero, one
// or two; a single crossing produces the intersection point.
fn clip_segment_to_plane(input: vector(ClipVertex), ref output: vector(ClipVertex),
                         normal: Vec2, offset: Fixed,
                         identifier: int) -> int {
    output.clear();
    if (input.size() < 2) {
        return 0;
    }
    let distance_0 = normal.dot(input[0].position) - offset;
    let distance_1 = normal.dot(input[1].position) - offset;
    if (distance_0 <= zero_fixed()) {
        output.push_back(make_clip_vertex(input[0].position, input[0].identifier));
    }
    if (distance_1 <= zero_fixed()) {
        output.push_back(make_clip_vertex(input[1].position, input[1].identifier));
    }
    if (distance_0 * distance_1 < zero_fixed()) {
        let t = distance_0 / (distance_0 - distance_1);
        let crossing = input[0].position + (input[1].position - input[0].position) * t;
        output.push_back(make_clip_vertex(crossing, identifier));
    }
    return output.size();
}

// Full polygon against polygon test.
//
// Scan both polygons' faces, keep the axis of greatest separation, and use the
// owner of that axis as the reference polygon. Clip the other polygon's most
// anti-parallel edge against the reference face's two side planes, and keep
// the clipped points that are behind the reference face.
fn collide_polygon_polygon(ref manifold: Manifold,
                           a: Polygon, frame_a: Transform,
                           b: Polygon, frame_b: Transform) {
    narrow_phase_test_count += 1;
    manifold.clear();
    let query_a = greatest_separation(a, frame_a, b, frame_b);
    if (query_a.separation > zero_fixed()) {
        manifold.state = ContactState.Separated;
        return;
    }
    let query_b = greatest_separation(b, frame_b, a, frame_a);
    if (query_b.separation > zero_fixed()) {
        manifold.state = ContactState.Separated;
        return;
    }

    // Prefer face A unless face B is clearly deeper, which stops the choice
    // of reference face flickering between steps for a near-symmetric contact
    // and so keeps warm starting effective.
    var reference_is_a: bool = true;
    var reference_edge = query_a.edge_index;
    var bias = raw_fixed(PENETRATION_SLOP_RAW);
    if (query_b.separation > query_a.separation + bias) {
        reference_is_a = false;
        reference_edge = query_b.edge_index;
        manifold.feature = FeatureKind.FaceB;
    } else {
        manifold.feature = FeatureKind.FaceA;
    }

    var incident_points: vector(ClipVertex);
    var clipped_once: vector(ClipVertex);
    var clipped_twice: vector(ClipVertex);

    if (reference_is_a) {
        let reference_normal = frame_a.apply_direction(a.normals[reference_edge]);
        let reference_v0 = frame_a.apply(a.vertex(reference_edge));
        let reference_v1 = frame_a.apply(a.vertex(reference_edge + 1));
        let side = (reference_v1 - reference_v0).normalised();
        find_incident_edge(reference_normal, b, frame_b, incident_points);
        let survived_once = clip_segment_to_plane(incident_points, clipped_once,
                                                  -side, -side.dot(reference_v0),
                                                  reference_edge * 4 + 2);
        if (survived_once < 2) {
            manifold.state = ContactState.Touching;
            return;
        }
        let survived_twice = clip_segment_to_plane(clipped_once, clipped_twice,
                                                   side, side.dot(reference_v1),
                                                   reference_edge * 4 + 3);
        if (survived_twice < 2) {
            manifold.state = ContactState.Touching;
            return;
        }
        manifold.normal = reference_normal + vec2_zero();
        let face_offset = reference_normal.dot(reference_v0);
        var index: int = 0;
        while (index < clipped_twice.size() and manifold.points.size() < MAX_MANIFOLD_POINTS) {
            let separation = reference_normal.dot(clipped_twice[index].position) - face_offset;
            if (separation <= zero_fixed()) {
                manifold.points.push_back(make_contact_point(clipped_twice[index].position,
                                                             separation,
                                                             clipped_twice[index].identifier));
            }
            index += 1;
        }
    } else {
        let reference_normal = frame_b.apply_direction(b.normals[reference_edge]);
        let reference_v0 = frame_b.apply(b.vertex(reference_edge));
        let reference_v1 = frame_b.apply(b.vertex(reference_edge + 1));
        let side = (reference_v1 - reference_v0).normalised();
        find_incident_edge(reference_normal, a, frame_a, incident_points);
        let survived_once = clip_segment_to_plane(incident_points, clipped_once,
                                                  -side, -side.dot(reference_v0),
                                                  reference_edge * 4 + 2);
        if (survived_once < 2) {
            manifold.state = ContactState.Touching;
            return;
        }
        let survived_twice = clip_segment_to_plane(clipped_once, clipped_twice,
                                                   side, side.dot(reference_v1),
                                                   reference_edge * 4 + 3);
        if (survived_twice < 2) {
            manifold.state = ContactState.Touching;
            return;
        }
        // The reference face belongs to B, so the manifold normal has to be
        // flipped back to point from A towards B.
        manifold.normal = -reference_normal;
        let face_offset = reference_normal.dot(reference_v0);
        var index: int = 0;
        while (index < clipped_twice.size() and manifold.points.size() < MAX_MANIFOLD_POINTS) {
            let separation = reference_normal.dot(clipped_twice[index].position) - face_offset;
            if (separation <= zero_fixed()) {
                manifold.points.push_back(make_contact_point(clipped_twice[index].position,
                                                             separation,
                                                             clipped_twice[index].identifier));
            }
            index += 1;
        }
    }

    if (manifold.points.size() == 0) {
        manifold.state = ContactState.Touching;
    } else {
        manifold.state = ContactState.Penetrating;
    }
}

// --- Dispatch -------------------------------------------------------------

// Route a body pair to the right narrow-phase routine and fill in the mixed
// material coefficients. The normal always ends up pointing from A to B, so
// the solver never has to care which routine produced it.
fn collide(ref manifold: Manifold,
           a: RigidBody, b: RigidBody) {
    manifold.friction = mix_dynamic_friction(a.material, b.material);
    manifold.restitution = mix_restitution(a.material, b.material);
    if (a.shape.kind == ShapeKind.Circle and b.shape.kind == ShapeKind.Circle) {
        collide_circle_circle(manifold, a.shape.circle, a.frame, b.shape.circle, b.frame);
        return;
    }
    if (a.shape.kind == ShapeKind.Circle) {
        // The polygon routine produces a normal pointing away from the
        // polygon, which is B here, so it has to be flipped.
        collide_circle_polygon(manifold, a.shape.circle, a.frame, b.shape.polygon, b.frame, true);
        return;
    }
    if (b.shape.kind == ShapeKind.Circle) {
        collide_circle_polygon(manifold, b.shape.circle, b.frame, a.shape.polygon, a.frame, false);
        return;
    }
    collide_polygon_polygon(manifold, a.shape.polygon, a.frame, b.shape.polygon, b.frame);
}

// Cheap rejection before the full test, using the two proxy boxes.
fn quick_reject(a: RigidBody, b: RigidBody) -> bool {
    return not a.bounds().overlaps(b.bounds());
}

// --- Sequential impulse solver --------------------------------------------
//
// The velocity solver runs a fixed number of Gauss-Seidel passes over the
// contact list. Each pass computes, for every contact point, the impulse that
// would exactly cancel the relative velocity along the constraint direction,
// clamps the accumulated impulse to its valid range, and applies the change.
//
// Clamping the accumulated total rather than the increment is what makes the
// solver converge instead of oscillating: an individual increment is allowed
// to be negative as long as the running total stays non-negative.

// Fraction of the remaining penetration removed per position pass. Removing
// all of it in one pass overshoots and makes stacks explode.
let BAUMGARTE_RAW: int = 13107;

// Maximum penetration removed in a single position pass, so that a badly
// overlapping pair separates over several steps instead of teleporting.
let MAX_LINEAR_CORRECTION_RAW: int = 13107;

// Default iteration counts.
let DEFAULT_VELOCITY_ITERATIONS: int = 8;
let DEFAULT_POSITION_ITERATIONS: int = 3;

// A resolved constraint, holding everything the inner loop needs so that the
// loop never has to reach back through a body index.
class ContactConstraint {

    // Effective mass along a direction for a contact point: the reciprocal of
    //     1/ma + 1/mb + (ra x d)^2 / Ia + (rb x d)^2 / Ib
    // which is the scalar mass the constraint sees along that direction.
    fn effective_mass(self, anchor_a: Vec2, anchor_b: Vec2,
                      direction: Vec2) -> Fixed {
        let cross_a = anchor_a.x * direction.y - anchor_a.y * direction.x;
        let cross_b = anchor_b.x * direction.y - anchor_b.y * direction.x;
        let total = self.inverse_mass_a + self.inverse_mass_b
            + self.inverse_inertia_a * cross_a * cross_a
            + self.inverse_inertia_b * cross_b * cross_b;
        if (total.raw <= 0) {
            return zero_fixed();
        }
        return total.reciprocal();
    }

    var manifold_index: int = 0;
    var body_a: int = 0;
    var body_b: int = 0;
    var inverse_mass_a: Fixed;
    var inverse_mass_b: Fixed;
    var inverse_inertia_a: Fixed;
    var inverse_inertia_b: Fixed;
    var centre_a: Vec2;
    var centre_b: Vec2;
    var friction: Fixed;
    var restitution: Fixed;
}

class ContactSolver {

    // --- Setup ------------------------------------------------------------

    // Build one constraint per manifold and precompute the effective masses
    // and the restitution bias. Everything here depends only on the geometry
    // and so is hoisted out of the iteration loop.
    fn prepare(ref self, ref bodies: vector(RigidBody),
               ref manifolds: vector(Manifold), dt: Fixed) {
        self.constraints.clear();
        var index: int = 0;
        while (index < manifolds.size()) {
            let ref manifold: auto = manifolds[index];
            if (not manifold.is_touching()) {
                index += 1;
            } else {
                var constraint_: ContactConstraint;
                constraint_.manifold_index = index;
                constraint_.body_a = manifold.body_a;
                constraint_.body_b = manifold.body_b;
                constraint_.friction = manifold.friction + zero_fixed();
                constraint_.restitution = manifold.restitution + zero_fixed();

                let ref body_a: auto = bodies[manifold.body_a];
                let ref body_b: auto = bodies[manifold.body_b];
                constraint_.inverse_mass_a = body_a.inverse_mass + zero_fixed();
                constraint_.inverse_mass_b = body_b.inverse_mass + zero_fixed();
                constraint_.inverse_inertia_a = body_a.inverse_inertia + zero_fixed();
                constraint_.inverse_inertia_b = body_b.inverse_inertia + zero_fixed();
                constraint_.centre_a = body_a.world_centre();
                constraint_.centre_b = body_b.world_centre();

                let tangent = manifold.tangent();
                var point_index: int = 0;
                while (point_index < manifold.points.size()) {
                    let ref point: auto = manifold.points[point_index];
                    point.anchor_a = point.position - constraint_.centre_a;
                    point.anchor_b = point.position - constraint_.centre_b;
                    point.local_a = body_a.to_local(point.position);
                    point.local_b = body_b.to_local(point.position);
                    point.normal_mass = constraint_.effective_mass(point.anchor_a, point.anchor_b,
                                                                  manifold.normal);
                    point.tangent_mass = constraint_.effective_mass(point.anchor_a, point.anchor_b,
                                                                   tangent);

                    // Restitution bias: the approach speed at the contact,
                    // scaled by the coefficient. Slow approaches get none, so
                    // that a resting stack does not buzz.
                    let velocity_a = body_a.velocity_at(point.position);
                    let velocity_b = body_b.velocity_at(point.position);
                    let approach = manifold.normal.x * (velocity_b.x - velocity_a.x)
                        + manifold.normal.y * (velocity_b.y - velocity_a.y);
                    if (approach < -raw_fixed(RESTITUTION_THRESHOLD_RAW)) {
                        point.velocity_bias = -constraint_.restitution * approach;
                    } else {
                        point.velocity_bias = zero_fixed();
                    }
                    point_index += 1;
                }
                self.constraints.push_back(constraint_);
                index += 1;
            }
        }
    }

    // --- Warm starting ----------------------------------------------------

    // Re-apply the impulses accumulated last step before the first iteration.
    // The solver is then starting from a solution that was nearly right,
    // which is worth several iterations of convergence for free.
    fn warm_start(ref self, ref bodies: vector(RigidBody),
                  manifolds: vector(Manifold)) {
        var index: int = 0;
        while (index < self.constraints.size()) {
            let ref constraint_: auto = self.constraints[index];
            let ref manifold: auto = manifolds[constraint_.manifold_index];
            let tangent = manifold.tangent();
            var point_index: int = 0;
            while (point_index < manifold.points.size()) {
                let ref point: auto = manifold.points[point_index];
                let impulse = make_vec2(
                    manifold.normal.x * point.normal_impulse + tangent.x * point.tangent_impulse,
                    manifold.normal.y * point.normal_impulse + tangent.y * point.tangent_impulse);
                let ref body_a: auto = bodies[constraint_.body_a];
                body_a.velocity.x = body_a.velocity.x - impulse.x * constraint_.inverse_mass_a;
                body_a.velocity.y = body_a.velocity.y - impulse.y * constraint_.inverse_mass_a;
                body_a.angular_velocity = body_a.angular_velocity
                    - constraint_.inverse_inertia_a
                    * (point.anchor_a.x * impulse.y - point.anchor_a.y * impulse.x);
                let ref body_b: auto = bodies[constraint_.body_b];
                body_b.velocity.x = body_b.velocity.x + impulse.x * constraint_.inverse_mass_b;
                body_b.velocity.y = body_b.velocity.y + impulse.y * constraint_.inverse_mass_b;
                body_b.angular_velocity = body_b.angular_velocity
                    + constraint_.inverse_inertia_b
                    * (point.anchor_b.x * impulse.y - point.anchor_b.y * impulse.x);
                point_index += 1;
            }
            index += 1;
        }
    }

    // --- Velocity iteration -----------------------------------------------

    // Friction first, then the normal. Solving friction against the previous
    // step's normal impulse rather than this iteration's is the standard
    // ordering and behaves much better on slopes.
    fn solve_velocity_once(ref self, ref bodies: vector(RigidBody),
                           ref manifolds: vector(Manifold)) {
        var index: int = 0;
        while (index < self.constraints.size()) {
            let ref constraint_: auto = self.constraints[index];
            let ref manifold: auto = manifolds[constraint_.manifold_index];
            let tangent = manifold.tangent();

            var point_index: int = 0;
            while (point_index < manifold.points.size()) {
                let ref point: auto = manifold.points[point_index];
                let ref body_a: auto = bodies[constraint_.body_a];
                let ref body_b: auto = bodies[constraint_.body_b];

                // Relative velocity at the contact, written out so that the
                // angular terms are visible: v + omega cross r for each body.
                let relative_x = body_b.velocity.x - body_b.angular_velocity * point.anchor_b.y
                    - body_a.velocity.x + body_a.angular_velocity * point.anchor_a.y;
                let relative_y = body_b.velocity.y + body_b.angular_velocity * point.anchor_b.x
                    - body_a.velocity.y - body_a.angular_velocity * point.anchor_a.x;

                // --- Friction along the tangent ---------------------------
                let tangent_speed = tangent.x * relative_x + tangent.y * relative_y;
                var tangent_delta = -point.tangent_mass * tangent_speed;
                let friction_limit = constraint_.friction * point.normal_impulse;
                let new_tangent = (point.tangent_impulse + tangent_delta)
                    .clamped(-friction_limit, friction_limit);
                tangent_delta = new_tangent - point.tangent_impulse;
                point.tangent_impulse = new_tangent + zero_fixed();

                let friction_impulse = make_vec2(tangent.x * tangent_delta, tangent.y * tangent_delta);
                body_a.velocity.x = body_a.velocity.x - friction_impulse.x * constraint_.inverse_mass_a;
                body_a.velocity.y = body_a.velocity.y - friction_impulse.y * constraint_.inverse_mass_a;
                body_a.angular_velocity = body_a.angular_velocity
                    - constraint_.inverse_inertia_a
                    * (point.anchor_a.x * friction_impulse.y - point.anchor_a.y * friction_impulse.x);
                body_b.velocity.x = body_b.velocity.x + friction_impulse.x * constraint_.inverse_mass_b;
                body_b.velocity.y = body_b.velocity.y + friction_impulse.y * constraint_.inverse_mass_b;
                body_b.angular_velocity = body_b.angular_velocity
                    + constraint_.inverse_inertia_b
                    * (point.anchor_b.x * friction_impulse.y - point.anchor_b.y * friction_impulse.x);

                // --- Normal impulse ---------------------------------------
                let after_x = body_b.velocity.x - body_b.angular_velocity * point.anchor_b.y
                    - body_a.velocity.x + body_a.angular_velocity * point.anchor_a.y;
                let after_y = body_b.velocity.y + body_b.angular_velocity * point.anchor_b.x
                    - body_a.velocity.y - body_a.angular_velocity * point.anchor_a.x;
                let normal_speed = manifold.normal.x * after_x + manifold.normal.y * after_y;
                var normal_delta = -point.normal_mass * (normal_speed - point.velocity_bias);
                // The accumulated total is clamped, not the increment.
                let new_normal = fixed_max(point.normal_impulse + normal_delta, zero_fixed());
                normal_delta = new_normal - point.normal_impulse;
                point.normal_impulse = new_normal + zero_fixed();

                let normal_impulse = make_vec2(manifold.normal.x * normal_delta,
                                               manifold.normal.y * normal_delta);
                body_a.velocity.x = body_a.velocity.x - normal_impulse.x * constraint_.inverse_mass_a;
                body_a.velocity.y = body_a.velocity.y - normal_impulse.y * constraint_.inverse_mass_a;
                body_a.angular_velocity = body_a.angular_velocity
                    - constraint_.inverse_inertia_a
                    * (point.anchor_a.x * normal_impulse.y - point.anchor_a.y * normal_impulse.x);
                body_b.velocity.x = body_b.velocity.x + normal_impulse.x * constraint_.inverse_mass_b;
                body_b.velocity.y = body_b.velocity.y + normal_impulse.y * constraint_.inverse_mass_b;
                body_b.angular_velocity = body_b.angular_velocity
                    + constraint_.inverse_inertia_b
                    * (point.anchor_b.x * normal_impulse.y - point.anchor_b.y * normal_impulse.x);

                solver_iteration_count += 1;
                point_index += 1;
            }
            index += 1;
        }
    }

    fn solve_velocity(ref self, ref bodies: vector(RigidBody),
                      ref manifolds: vector(Manifold), iterations: int) {
        var pass: int = 0;
        while (pass < iterations) {
            self.solve_velocity_once(bodies, manifolds);
            pass += 1;
        }
    }

    // --- Position iteration -----------------------------------------------

    // Non-linear Gauss-Seidel on the positions. Rather than adding a bias to
    // the velocity constraint, the bodies are moved directly, which removes
    // the energy that a Baumgarte velocity bias would otherwise inject.
    //
    // Returns the deepest remaining penetration, so that the caller can stop
    // early once every contact is within the slop.
    fn solve_position_once(ref self, ref bodies: vector(RigidBody),
                           ref manifolds: vector(Manifold)) -> Fixed {
        var deepest = zero_fixed();
        let slop = raw_fixed(PENETRATION_SLOP_RAW);
        let baumgarte = raw_fixed(BAUMGARTE_RAW);
        let max_correction = raw_fixed(MAX_LINEAR_CORRECTION_RAW);
        var index: int = 0;
        while (index < self.constraints.size()) {
            let ref constraint_: auto = self.constraints[index];
            let ref manifold: auto = manifolds[constraint_.manifold_index];
            var point_index: int = 0;
            while (point_index < manifold.points.size()) {
                let ref point: auto = manifold.points[point_index];
                let ref body_a: auto = bodies[constraint_.body_a];
                let ref body_b: auto = bodies[constraint_.body_b];

                // Recompute the world anchors from the local ones, because
                // the bodies have moved since the manifold was built.
                let world_a = body_a.to_world(point.local_a);
                let world_b = body_b.to_world(point.local_b);
                let separation = manifold.normal.x * (world_b.x - world_a.x)
                    + manifold.normal.y * (world_b.y - world_a.y)
                    + point.separation;
                deepest = fixed_min(deepest, separation);

                let correction = (baumgarte * (separation + slop))
                    .clamped(-max_correction, zero_fixed());
                let anchor_a = world_a - body_a.world_centre();
                let anchor_b = world_b - body_b.world_centre();
                let mass = constraint_.effective_mass(anchor_a, anchor_b, manifold.normal);
                let magnitude = -mass * correction;
                let push = make_vec2(manifold.normal.x * magnitude, manifold.normal.y * magnitude);

                body_a.frame.position.x = body_a.frame.position.x - push.x * constraint_.inverse_mass_a;
                body_a.frame.position.y = body_a.frame.position.y - push.y * constraint_.inverse_mass_a;
                body_a.frame.angle = body_a.frame.angle
                    - constraint_.inverse_inertia_a * (anchor_a.x * push.y - anchor_a.y * push.x);
                body_a.frame.refresh();

                body_b.frame.position.x = body_b.frame.position.x + push.x * constraint_.inverse_mass_b;
                body_b.frame.position.y = body_b.frame.position.y + push.y * constraint_.inverse_mass_b;
                body_b.frame.angle = body_b.frame.angle
                    + constraint_.inverse_inertia_b * (anchor_b.x * push.y - anchor_b.y * push.x);
                body_b.frame.refresh();

                point_index += 1;
            }
            index += 1;
        }
        return deepest;
    }

    fn solve_position(ref self, ref bodies: vector(RigidBody),
                      ref manifolds: vector(Manifold), iterations: int) -> bool {
        let target = -raw_fixed(PENETRATION_SLOP_RAW) * 3;
        var pass: int = 0;
        var settled: bool = false;
        while (pass < iterations and not settled) {
            let deepest = self.solve_position_once(bodies, manifolds);
            if (deepest >= target) {
                settled = true;
            }
            pass += 1;
        }
        return settled;
    }

    // --- Diagnostics ------------------------------------------------------

    fn constraint_count(self) -> int {
        return self.constraints.size();
    }

    // Sum of every normal impulse in the solver, which is the total normal
    // force times the timestep and should equal the weight the ground is
    // holding up in a resting stack.
    fn total_normal_impulse(self, manifolds: vector(Manifold)) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.constraints.size()) {
            total = total + manifolds[self.constraints[index].manifold_index].total_normal_impulse();
            index += 1;
        }
        return total;
    }

    var constraints: vector(ContactConstraint);
}

// --- Joints ---------------------------------------------------------------
//
// Both joints follow the same shape as the contact solver: an initialise
// step that computes the effective mass and the bias, a warm start that
// re-applies last step's impulse, and an iteration step that computes and
// clamps an impulse increment.

// Fraction of joint position error removed per substep.
let JOINT_BAUMGARTE_RAW: int = 13107;

// A distance joint above this stretch factor reports itself broken.
let JOINT_BREAK_STRETCH_RAW: int = 196608;

// A distance constraint between a point on each of two bodies. With a
// stiffness and a damping ratio it becomes a soft constraint, which is how a
// spring is spelled; with both at zero it is rigid.
class DistanceJoint {

    // Effective mass along the joint axis, which is the same formula as a
    // contact normal constraint.
    fn axial_mass(self, a: RigidBody, b: RigidBody,
                  anchor_a: Vec2, anchor_b: Vec2) -> Fixed {
        let cross_a = anchor_a.x * self.axis.y - anchor_a.y * self.axis.x;
        let cross_b = anchor_b.x * self.axis.y - anchor_b.y * self.axis.x;
        let total = a.inverse_mass + b.inverse_mass
            + a.inverse_inertia * cross_a * cross_a
            + b.inverse_inertia * cross_b * cross_b;
        if (total.raw <= 0) {
            return zero_fixed();
        }
        return total.reciprocal();
    }

    // Compute the axis, the current length and the bias for this substep.
    fn prepare(ref self, ref bodies: vector(RigidBody), dt: Fixed) {
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];
        let world_a = body_a.to_world(self.local_anchor_a);
        let world_b = body_b.to_world(self.local_anchor_b);
        self.anchor_a = world_a - body_a.world_centre();
        self.anchor_b = world_b - body_b.world_centre();

        let separation = world_b - world_a;
        self.current_length = separation.length();
        if (self.current_length.raw <= LINEAR_EPSILON_RAW) {
            self.axis = vec2_unit_x();
        } else {
            self.axis = separation / self.current_length;
        }
        self.mass = self.axial_mass(body_a, body_b, self.anchor_a, self.anchor_b);

        let error = self.current_length - self.rest_length;
        if (self.stiffness.raw > 0) {
            // Soft constraint: the bias pulls towards the rest length at a
            // rate set by the stiffness, and the damping term is folded into
            // the effective mass through the gamma softening parameter.
            let omega = self.stiffness;
            let zeta = self.damping;
            let c = dt * omega * (dt * omega + zeta * 2);
            if (c.raw != 0) {
                self.gamma = (dt * (dt * omega * omega + omega * zeta * 2)).reciprocal();
            } else {
                self.gamma = zero_fixed();
            }
            self.bias = error * dt * omega * omega * self.gamma;
            if ((self.mass + self.gamma).raw != 0) {
                self.mass = (self.mass.reciprocal() + self.gamma).reciprocal();
            }
        } else {
            self.gamma = zero_fixed();
            self.bias = error * raw_fixed(JOINT_BAUMGARTE_RAW) / dt;
        }

        if (self.current_length > self.rest_length * 2 + raw_fixed(JOINT_BREAK_STRETCH_RAW)) {
            self.status = JointStatus.Broken;
        } else {
            self.status = JointStatus.Satisfied;
        }
    }

    fn warm_start(ref self, ref bodies: vector(RigidBody)) {
        let impulse = self.axis * self.accumulated_impulse;
        let ref body_a: auto = bodies[self.body_a];
        body_a.velocity.x = body_a.velocity.x - impulse.x * body_a.inverse_mass;
        body_a.velocity.y = body_a.velocity.y - impulse.y * body_a.inverse_mass;
        body_a.angular_velocity = body_a.angular_velocity
            - body_a.inverse_inertia * (self.anchor_a.x * impulse.y - self.anchor_a.y * impulse.x);
        let ref body_b: auto = bodies[self.body_b];
        body_b.velocity.x = body_b.velocity.x + impulse.x * body_b.inverse_mass;
        body_b.velocity.y = body_b.velocity.y + impulse.y * body_b.inverse_mass;
        body_b.angular_velocity = body_b.angular_velocity
            + body_b.inverse_inertia * (self.anchor_b.x * impulse.y - self.anchor_b.y * impulse.x);
    }

    fn solve_velocity(ref self, ref bodies: vector(RigidBody)) {
        if (self.status == JointStatus.Broken) {
            return;
        }
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];
        let relative_x = body_b.velocity.x - body_b.angular_velocity * self.anchor_b.y
            - body_a.velocity.x + body_a.angular_velocity * self.anchor_a.y;
        let relative_y = body_b.velocity.y + body_b.angular_velocity * self.anchor_b.x
            - body_a.velocity.y - body_a.angular_velocity * self.anchor_a.x;
        let axial_speed = self.axis.x * relative_x + self.axis.y * relative_y;
        var delta = -self.mass * (axial_speed + self.bias + self.gamma * self.accumulated_impulse);

        // A rope only pulls; a rigid rod both pulls and pushes.
        if (self.rope_only) {
            let total = fixed_min(self.accumulated_impulse + delta, zero_fixed());
            delta = total - self.accumulated_impulse;
            self.accumulated_impulse = total + zero_fixed();
        } else {
            self.accumulated_impulse = self.accumulated_impulse + delta;
        }

        let impulse = make_vec2(self.axis.x * delta, self.axis.y * delta);
        body_a.velocity.x = body_a.velocity.x - impulse.x * body_a.inverse_mass;
        body_a.velocity.y = body_a.velocity.y - impulse.y * body_a.inverse_mass;
        body_a.angular_velocity = body_a.angular_velocity
            - body_a.inverse_inertia * (self.anchor_a.x * impulse.y - self.anchor_a.y * impulse.x);
        body_b.velocity.x = body_b.velocity.x + impulse.x * body_b.inverse_mass;
        body_b.velocity.y = body_b.velocity.y + impulse.y * body_b.inverse_mass;
        body_b.angular_velocity = body_b.angular_velocity
            + body_b.inverse_inertia * (self.anchor_b.x * impulse.y - self.anchor_b.y * impulse.x);
    }

    // Direct position correction for the rigid case. A soft joint skips this,
    // because its whole point is to allow error.
    fn solve_position(ref self, ref bodies: vector(RigidBody)) -> bool {
        if (self.stiffness.raw > 0 or self.status == JointStatus.Broken) {
            return true;
        }
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];
        let world_a = body_a.to_world(self.local_anchor_a);
        let world_b = body_b.to_world(self.local_anchor_b);
        let separation = world_b - world_a;
        let length = separation.length();
        if (length.raw <= LINEAR_EPSILON_RAW) {
            return true;
        }
        let unit = separation / length;
        let error = (length - self.rest_length).clamped(-raw_fixed(MAX_LINEAR_CORRECTION_RAW),
                                                    raw_fixed(MAX_LINEAR_CORRECTION_RAW));
        let anchor_a = world_a - body_a.world_centre();
        let anchor_b = world_b - body_b.world_centre();
        let mass = self.axial_mass(body_a, body_b, anchor_a, anchor_b);
        let push = unit * (-mass * error);
        body_a.frame.position = body_a.frame.position - push * body_a.inverse_mass;
        body_b.frame.position = body_b.frame.position + push * body_b.inverse_mass;
        body_a.frame.refresh();
        body_b.frame.refresh();
        if (error.magnitude().raw > PENETRATION_SLOP_RAW) {
            self.status = JointStatus.Corrected;
            return false;
        }
        return true;
    }

    fn stretch_ratio(self) -> Fixed {
        if (self.rest_length.raw == 0) {
            return zero_fixed();
        }
        return self.current_length / self.rest_length;
    }

    var body_a: int = 0;
    var body_b: int = 0;
    var local_anchor_a: Vec2;
    var local_anchor_b: Vec2;
    var anchor_a: Vec2;
    var anchor_b: Vec2;
    var axis: Vec2;
    var rest_length: Fixed;
    var current_length: Fixed;
    var stiffness: Fixed;
    var damping: Fixed;
    var gamma: Fixed;
    var bias: Fixed;
    var mass: Fixed;
    var accumulated_impulse: Fixed;
    var rope_only: bool = false;
    var status: JointStatus = JointStatus.Satisfied;
}

fn make_distance_joint(body_a: int, body_b: int, local_a: Vec2,
                       local_b: Vec2, rest_length: Fixed) -> DistanceJoint {
    var result: DistanceJoint;
    result.body_a = body_a;
    result.body_b = body_b;
    result.local_anchor_a = local_a + vec2_zero();
    result.local_anchor_b = local_b + vec2_zero();
    result.rest_length = rest_length + zero_fixed();
    result.axis = vec2_unit_x();
    return result;
}

fn make_spring_joint(body_a: int, body_b: int, local_a: Vec2,
                     local_b: Vec2, rest_length: Fixed,
                     stiffness: Fixed, damping: Fixed) -> DistanceJoint {
    var result = make_distance_joint(body_a, body_b, local_a, local_b, rest_length);
    result.stiffness = stiffness + zero_fixed();
    result.damping = damping + zero_fixed();
    return result;
}

// A revolute joint pins two points together and lets the bodies spin freely
// about the shared point. The constraint is two-dimensional, so the effective
// mass is a 2x2 matrix and the impulse is a vector, which is exactly what
// Mat2.solve is for.
class RevoluteJoint {

    fn prepare(ref self, ref bodies: vector(RigidBody), dt: Fixed) {
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];
        let world_a = body_a.to_world(self.local_anchor_a);
        let world_b = body_b.to_world(self.local_anchor_b);
        self.anchor_a = world_a - body_a.world_centre();
        self.anchor_b = world_b - body_b.world_centre();

        // The effective mass matrix for a point-to-point constraint:
        //
        //     K = (1/ma + 1/mb) I
        //       + 1/Ia [ ra.y^2, -ra.x ra.y ; -ra.x ra.y, ra.x^2 ]
        //       + 1/Ib [ rb.y^2, -rb.x rb.y ; -rb.x rb.y, rb.x^2 ]
        let scalar = body_a.inverse_mass + body_b.inverse_mass;
        let xx = scalar
            + body_a.inverse_inertia * self.anchor_a.y * self.anchor_a.y
            + body_b.inverse_inertia * self.anchor_b.y * self.anchor_b.y;
        let xy = -body_a.inverse_inertia * self.anchor_a.x * self.anchor_a.y
            - body_b.inverse_inertia * self.anchor_b.x * self.anchor_b.y;
        let yy = scalar
            + body_a.inverse_inertia * self.anchor_a.x * self.anchor_a.x
            + body_b.inverse_inertia * self.anchor_b.x * self.anchor_b.x;
        self.mass = make_mat2(make_vec2(xx, xy), make_vec2(xy, yy));

        // Positional drift, fed back as a velocity bias.
        let drift = world_b - world_a;
        self.bias = drift * raw_fixed(JOINT_BAUMGARTE_RAW) / dt;

        // The angular part, if a motor or a limit is active.
        let angular_total = body_a.inverse_inertia + body_b.inverse_inertia;
        if (angular_total.raw > 0) {
            self.angular_mass = angular_total.reciprocal();
        } else {
            self.angular_mass = zero_fixed();
        }
        self.relative_angle = normalise_angle(body_b.frame.angle - body_a.frame.angle - self.reference_angle);
    }

    fn warm_start(ref self, ref bodies: vector(RigidBody)) {
        let ref body_a: auto = bodies[self.body_a];
        body_a.velocity.x = body_a.velocity.x - self.accumulated_impulse.x * body_a.inverse_mass;
        body_a.velocity.y = body_a.velocity.y - self.accumulated_impulse.y * body_a.inverse_mass;
        body_a.angular_velocity = body_a.angular_velocity
            - body_a.inverse_inertia
            * (self.anchor_a.x * self.accumulated_impulse.y - self.anchor_a.y * self.accumulated_impulse.x)
            - body_a.inverse_inertia * self.accumulated_motor_impulse;
        let ref body_b: auto = bodies[self.body_b];
        body_b.velocity.x = body_b.velocity.x + self.accumulated_impulse.x * body_b.inverse_mass;
        body_b.velocity.y = body_b.velocity.y + self.accumulated_impulse.y * body_b.inverse_mass;
        body_b.angular_velocity = body_b.angular_velocity
            + body_b.inverse_inertia
            * (self.anchor_b.x * self.accumulated_impulse.y - self.anchor_b.y * self.accumulated_impulse.x)
            + body_b.inverse_inertia * self.accumulated_motor_impulse;
    }

    fn solve_velocity(ref self, ref bodies: vector(RigidBody), dt: Fixed) {
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];

        // --- Motor, if enabled --------------------------------------------
        if (self.motor_enabled) {
            let relative_speed = body_b.angular_velocity - body_a.angular_velocity - self.motor_speed;
            var motor_delta = -self.angular_mass * relative_speed;
            let budget = self.max_motor_torque * dt;
            let total = (self.accumulated_motor_impulse + motor_delta).clamped(-budget, budget);
            motor_delta = total - self.accumulated_motor_impulse;
            self.accumulated_motor_impulse = total + zero_fixed();
            body_a.angular_velocity = body_a.angular_velocity - motor_delta * body_a.inverse_inertia;
            body_b.angular_velocity = body_b.angular_velocity + motor_delta * body_b.inverse_inertia;
        }

        // --- Angular limits, if enabled -----------------------------------
        if (self.limit_enabled) {
            if (self.relative_angle < self.lower_limit) {
                let excess = self.relative_angle - self.lower_limit;
                let speed = body_b.angular_velocity - body_a.angular_velocity;
                var delta = -self.angular_mass * (speed + excess * raw_fixed(JOINT_BAUMGARTE_RAW) / dt);
                let total = fixed_max(self.accumulated_limit_impulse + delta, zero_fixed());
                delta = total - self.accumulated_limit_impulse;
                self.accumulated_limit_impulse = total + zero_fixed();
                body_a.angular_velocity = body_a.angular_velocity - delta * body_a.inverse_inertia;
                body_b.angular_velocity = body_b.angular_velocity + delta * body_b.inverse_inertia;
            } else {
                if (self.relative_angle > self.upper_limit) {
                    let excess = self.relative_angle - self.upper_limit;
                    let speed = body_b.angular_velocity - body_a.angular_velocity;
                    var delta = -self.angular_mass * (speed + excess * raw_fixed(JOINT_BAUMGARTE_RAW) / dt);
                    let total = fixed_min(self.accumulated_limit_impulse + delta, zero_fixed());
                    delta = total - self.accumulated_limit_impulse;
                    self.accumulated_limit_impulse = total + zero_fixed();
                    body_a.angular_velocity = body_a.angular_velocity - delta * body_a.inverse_inertia;
                    body_b.angular_velocity = body_b.angular_velocity + delta * body_b.inverse_inertia;
                }
            }
        }

        // --- The point-to-point constraint itself -------------------------
        let relative_x = body_b.velocity.x - body_b.angular_velocity * self.anchor_b.y
            - body_a.velocity.x + body_a.angular_velocity * self.anchor_a.y;
        let relative_y = body_b.velocity.y + body_b.angular_velocity * self.anchor_b.x
            - body_a.velocity.y - body_a.angular_velocity * self.anchor_a.x;
        let residual = make_vec2(relative_x + self.bias.x, relative_y + self.bias.y);
        let impulse = self.mass.solve(-residual);
        self.accumulated_impulse = self.accumulated_impulse + impulse;

        body_a.velocity.x = body_a.velocity.x - impulse.x * body_a.inverse_mass;
        body_a.velocity.y = body_a.velocity.y - impulse.y * body_a.inverse_mass;
        body_a.angular_velocity = body_a.angular_velocity
            - body_a.inverse_inertia * (self.anchor_a.x * impulse.y - self.anchor_a.y * impulse.x);
        body_b.velocity.x = body_b.velocity.x + impulse.x * body_b.inverse_mass;
        body_b.velocity.y = body_b.velocity.y + impulse.y * body_b.inverse_mass;
        body_b.angular_velocity = body_b.angular_velocity
            + body_b.inverse_inertia * (self.anchor_b.x * impulse.y - self.anchor_b.y * impulse.x);
    }

    fn solve_position(ref self, ref bodies: vector(RigidBody)) -> bool {
        let ref body_a: auto = bodies[self.body_a];
        let ref body_b: auto = bodies[self.body_b];
        let world_a = body_a.to_world(self.local_anchor_a);
        let world_b = body_b.to_world(self.local_anchor_b);
        let drift = world_b - world_a;
        if (drift.length_squared().raw <= LINEAR_EPSILON_SQUARED_RAW) {
            return true;
        }
        let anchor_a = world_a - body_a.world_centre();
        let anchor_b = world_b - body_b.world_centre();
        let scalar = body_a.inverse_mass + body_b.inverse_mass;
        let xx = scalar
            + body_a.inverse_inertia * anchor_a.y * anchor_a.y
            + body_b.inverse_inertia * anchor_b.y * anchor_b.y;
        let xy = -body_a.inverse_inertia * anchor_a.x * anchor_a.y
            - body_b.inverse_inertia * anchor_b.x * anchor_b.y;
        let yy = scalar
            + body_a.inverse_inertia * anchor_a.x * anchor_a.x
            + body_b.inverse_inertia * anchor_b.x * anchor_b.x;
        let matrix = make_mat2(make_vec2(xx, xy), make_vec2(xy, yy));
        let push = matrix.solve(-drift);
        body_a.frame.position = body_a.frame.position - push * body_a.inverse_mass;
        body_a.frame.angle = body_a.frame.angle
            - body_a.inverse_inertia * (anchor_a.x * push.y - anchor_a.y * push.x);
        body_a.frame.refresh();
        body_b.frame.position = body_b.frame.position + push * body_b.inverse_mass;
        body_b.frame.angle = body_b.frame.angle
            + body_b.inverse_inertia * (anchor_b.x * push.y - anchor_b.y * push.x);
        body_b.frame.refresh();
        return drift.manhattan_length().raw <= PENETRATION_SLOP_RAW;
    }

    var body_a: int = 0;
    var body_b: int = 0;
    var local_anchor_a: Vec2;
    var local_anchor_b: Vec2;
    var anchor_a: Vec2;
    var anchor_b: Vec2;
    var mass: Mat2;
    var bias: Vec2;
    var accumulated_impulse: Vec2;
    var angular_mass: Fixed;
    var accumulated_motor_impulse: Fixed;
    var accumulated_limit_impulse: Fixed;
    var reference_angle: Fixed;
    var relative_angle: Fixed;
    var motor_speed: Fixed;
    var max_motor_torque: Fixed;
    var lower_limit: Fixed;
    var upper_limit: Fixed;
    var motor_enabled: bool = false;
    var limit_enabled: bool = false;
}

fn make_revolute_joint(body_a: int, body_b: int, local_a: Vec2,
                       local_b: Vec2) -> RevoluteJoint {
    var result: RevoluteJoint;
    result.body_a = body_a;
    result.body_b = body_b;
    result.local_anchor_a = local_a + vec2_zero();
    result.local_anchor_b = local_b + vec2_zero();
    result.mass = mat2_identity();
    return result;
}

fn make_motorised_joint(body_a: int, body_b: int, local_a: Vec2,
                        local_b: Vec2, speed: Fixed,
                        torque: Fixed) -> RevoluteJoint {
    var result = make_revolute_joint(body_a, body_b, local_a, local_b);
    result.motor_enabled = true;
    result.motor_speed = speed + zero_fixed();
    result.max_motor_torque = torque + zero_fixed();
    return result;
}

// --- Particle system ------------------------------------------------------
//
// Particles are integrated with position Verlet rather than with the
// semi-implicit Euler the rigid bodies use. Verlet stores the previous
// position instead of a velocity, so a positional constraint can be satisfied
// by moving the particle directly and the velocity follows automatically. That
// is what makes a cloth or a rope of a hundred springs stable at stiffnesses
// that would blow up an explicit spring force.
//
//     next = position + (position - previous) * damping + acceleration * dt^2
//
// The velocity is implied by the gap between position and previous, so it must
// never be written directly; use displace() instead, which moves both.

// Constraint relaxation passes per particle step. Verlet constraints converge
// by relaxation, so more passes means a stiffer cloth.
let DEFAULT_RELAXATION_PASSES: int = 4;

// A spring that has stretched beyond this multiple of its rest length snaps.
let SPRING_TEAR_RATIO_RAW: int = 262144;

class Particle {

    // Implied velocity, as a per-second rate rather than a per-step delta.
    fn velocity(self, dt: Fixed) -> Vec2 {
        if (dt.raw == 0) {
            return vec2_zero();
        }
        return (self.position - self.previous) / dt;
    }

    // Move the particle without changing its implied velocity. Every
    // constraint in the system goes through here.
    fn displace(ref self, delta: Vec2) {
        self.position = self.position + delta;
        self.previous = self.previous + delta;
    }

    // Move the particle and let the implied velocity absorb the change, which
    // is what a collision response wants.
    fn shift(ref self, delta: Vec2) {
        self.position = self.position + delta;
    }

    // Set the implied velocity by moving the previous position.
    fn set_velocity(ref self, v: Vec2, dt: Fixed) {
        self.previous = self.position - v * dt;
    }

    fn add_force(ref self, f: Vec2) {
        self.acceleration = self.acceleration + f * self.inverse_mass;
    }

    // One Verlet step. The damping factor multiplies the implied velocity,
    // and the acceleration term is scaled by dt squared because Verlet
    // integrates acceleration to position in one go.
    fn integrate(ref self, gravity: Vec2, dt: Fixed,
                 damping: Fixed) {
        if (self.pinned or self.inverse_mass.raw == 0) {
            self.previous = self.position + vec2_zero();
            self.acceleration = vec2_zero();
            return;
        }
        let implied = (self.position - self.previous) * damping;
        let total_acceleration = self.acceleration + gravity;
        let next = make_vec2(
            self.position.x + implied.x + total_acceleration.x * dt * dt,
            self.position.y + implied.y + total_acceleration.y * dt * dt);
        self.previous = self.position + vec2_zero();
        self.position = next + vec2_zero();
        self.acceleration = vec2_zero();
    }

    fn kinetic_energy(self, dt: Fixed) -> Fixed {
        if (self.inverse_mass.raw == 0) {
            return zero_fixed();
        }
        return self.velocity(dt).length_squared() / (self.inverse_mass * 2);
    }

    var position: Vec2;
    var previous: Vec2;
    var acceleration: Vec2;
    var inverse_mass: Fixed;
    var radius: Fixed;
    var pinned: bool = false;
    var alive: bool = true;
    var group: int = 0;
}

fn make_particle(position: Vec2, mass: Fixed) -> Particle {
    var result: Particle;
    result.position = position + vec2_zero();
    result.previous = position + vec2_zero();
    result.radius = raw_fixed(FIXED_QUARTER);
    if (mass.raw > 0) {
        result.inverse_mass = mass.reciprocal();
    }
    return result;
}

fn make_pinned_particle(position: Vec2) -> Particle {
    var result = make_particle(position, one_fixed());
    result.pinned = true;
    result.inverse_mass = zero_fixed();
    return result;
}

// A distance constraint between two particles, solved by relaxation rather
// than by an impulse. stiffness in [0, 1] is the fraction of the error
// removed per pass.
class Spring {

    fn current_length(self, particles: vector(Particle)) -> Fixed {
        return (particles[self.particle_b].position - particles[self.particle_a].position).length();
    }

    fn strain(self, particles: vector(Particle)) -> Fixed {
        if (self.rest_length.raw == 0) {
            return zero_fixed();
        }
        return (self.current_length(particles) - self.rest_length) / self.rest_length;
    }

    // One relaxation pass. The correction is split between the two particles
    // in proportion to their inverse masses, so a heavy particle barely moves
    // and a pinned one does not move at all.
    fn relax(ref self, ref particles: vector(Particle)) {
        if (self.broken) {
            return;
        }
        let ref a: auto = particles[self.particle_a];
        let ref b: auto = particles[self.particle_b];
        let separation = b.position - a.position;
        let length = separation.length();
        if (length.raw <= LINEAR_EPSILON_RAW) {
            return;
        }
        let total_inverse_mass = a.inverse_mass + b.inverse_mass;
        if (total_inverse_mass.raw == 0) {
            return;
        }
        let error = length - self.rest_length;
        if (self.rest_length.raw > 0 and length > self.rest_length * raw_fixed(SPRING_TEAR_RATIO_RAW)) {
            self.broken = true;
            return;
        }
        let unit = separation / length;
        let correction = unit * (error * self.stiffness / total_inverse_mass);
        self.last_tension = error * self.stiffness;
        let ref mover_a: auto = particles[self.particle_a];
        mover_a.position = mover_a.position + correction * mover_a.inverse_mass;
        let ref mover_b: auto = particles[self.particle_b];
        mover_b.position = mover_b.position - correction * mover_b.inverse_mass;
    }

    // Potential energy stored in the spring, as k x^2 / 2 with k taken as the
    // stiffness. Approximate, but enough for the energy audit below.
    fn potential_energy(self, particles: vector(Particle)) -> Fixed {
        let error = self.current_length(particles) - self.rest_length;
        return self.stiffness * error * error / 2;
    }

    var particle_a: int = 0;
    var particle_b: int = 0;
    var rest_length: Fixed;
    var stiffness: Fixed;
    var last_tension: Fixed;
    var broken: bool = false;
}

fn make_spring(a: int, b: int, rest_length: Fixed,
               stiffness: Fixed) -> Spring {
    var result: Spring;
    result.particle_a = a;
    result.particle_b = b;
    result.rest_length = rest_length + zero_fixed();
    result.stiffness = stiffness + zero_fixed();
    return result;
}

// Ordering by rest length, which the debug tooling uses to find the springs
// most likely to be the stiff ones in a mixed cloth.
impl Spring as Core.OrderedWith(Spring) {
  fn Less(self, other: Spring) -> bool {
      return self.rest_length < other.rest_length;
  }
}

class ParticleSystem {

    // --- Construction -----------------------------------------------------

    fn add_particle(ref self, position: Vec2, mass: Fixed) -> int {
        let index = self.particles.size();
        self.particles.push_back(make_particle(position, mass));
        return index;
    }

    fn add_pinned(ref self, position: Vec2) -> int {
        let index = self.particles.size();
        self.particles.push_back(make_pinned_particle(position));
        return index;
    }

    // Connect two particles with a spring whose rest length is their current
    // separation, which is what building a cloth from a grid wants.
    fn connect(ref self, a: int, b: int, stiffness: Fixed) -> int {
        let rest = (self.particles[b].position - self.particles[a].position).length();
        let index = self.springs.size();
        self.springs.push_back(make_spring(a, b, rest, stiffness));
        return index;
    }

    fn connect_at(ref self, a: int, b: int, rest: Fixed,
                  stiffness: Fixed) -> int {
        let index = self.springs.size();
        self.springs.push_back(make_spring(a, b, rest, stiffness));
        return index;
    }

    // --- Simulation -------------------------------------------------------

    fn integrate(ref self, gravity: Vec2, dt: Fixed) {
        var index: int = 0;
        while (index < self.particles.size()) {
            self.particles[index].integrate(gravity, dt, self.damping);
            index += 1;
        }
    }

    fn relax_springs(ref self) {
        var pass: int = 0;
        while (pass < self.relaxation_passes) {
            var index: int = 0;
            while (index < self.springs.size()) {
                self.springs[index].relax(self.particles);
                index += 1;
            }
            pass += 1;
        }
    }

    // Keep every particle inside the world box, absorbing the normal
    // component of its implied velocity as it goes.
    fn apply_bounds(ref self, bounds: AABB, restitution: Fixed) {
        var index: int = 0;
        while (index < self.particles.size()) {
            let ref particle: auto = self.particles[index];
            if (particle.position.x < bounds.lower.x + particle.radius) {
                particle.position.x = bounds.lower.x + particle.radius;
                particle.previous.x = particle.position.x
                    + (particle.position.x - particle.previous.x) * restitution;
            }
            if (particle.position.x > bounds.upper.x - particle.radius) {
                particle.position.x = bounds.upper.x - particle.radius;
                particle.previous.x = particle.position.x
                    + (particle.position.x - particle.previous.x) * restitution;
            }
            if (particle.position.y < bounds.lower.y + particle.radius) {
                particle.position.y = bounds.lower.y + particle.radius;
                particle.previous.y = particle.position.y
                    + (particle.position.y - particle.previous.y) * restitution;
            }
            if (particle.position.y > bounds.upper.y - particle.radius) {
                particle.position.y = bounds.upper.y - particle.radius;
                particle.previous.y = particle.position.y
                    + (particle.position.y - particle.previous.y) * restitution;
            }
            index += 1;
        }
    }

    // Push overlapping particles apart. Quadratic in the particle count, so
    // this is only for the small systems; a large one would route through the
    // spatial hash the same way the rigid bodies do.
    fn resolve_particle_overlap(ref self) {
        var first: int = 0;
        while (first < self.particles.size()) {
            var second = first + 1;
            while (second < self.particles.size()) {
                let ref a: auto = self.particles[first];
                let ref b: auto = self.particles[second];
                let separation = b.position - a.position;
                let combined = a.radius + b.radius;
                let distance_squared = separation.length_squared();
                if (distance_squared < combined * combined and distance_squared.raw > 0) {
                    let distance = distance_squared.square_root();
                    let overlap = combined - distance;
                    let unit = separation / distance;
                    let total_inverse_mass = a.inverse_mass + b.inverse_mass;
                    if (total_inverse_mass.raw > 0) {
                        let correction = unit * (overlap / total_inverse_mass);
                        let ref mover_a: auto = self.particles[first];
                        mover_a.position = mover_a.position - correction * mover_a.inverse_mass;
                        let ref mover_b: auto = self.particles[second];
                        mover_b.position = mover_b.position + correction * mover_b.inverse_mass;
                    }
                }
                second += 1;
            }
            first += 1;
        }
    }

    // Push particles out of the rigid bodies they have sunk into. Particles
    // do not push back, which keeps the two systems one-way coupled and so
    // keeps the rigid solver's energy audit honest.
    fn resolve_body_overlap(ref self, bodies: vector(RigidBody)) {
        var index: int = 0;
        while (index < self.particles.size()) {
            var body_index: int = 0;
            while (body_index < bodies.size()) {
                let ref body: auto = bodies[body_index];
                let ref particle: auto = self.particles[index];
                if (body.shape.contains_point(body.frame, particle.position)) {
                    let direction = (particle.position - body.world_centre()).normalised();
                    particle.position = particle.position + direction * particle.radius;
                }
                body_index += 1;
            }
            index += 1;
        }
    }

    fn step(ref self, gravity: Vec2, dt: Fixed,
            bounds: AABB) {
        self.integrate(gravity, dt);
        self.relax_springs();
        self.resolve_particle_overlap();
        self.apply_bounds(bounds, self.bounce);
    }

    // --- Energy -----------------------------------------------------------

    fn kinetic_energy(self, dt: Fixed) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.particles.size()) {
            total = total + self.particles[index].kinetic_energy(dt);
            index += 1;
        }
        return total;
    }

    fn spring_energy(self) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.springs.size()) {
            total = total + self.springs[index].potential_energy(self.particles);
            index += 1;
        }
        return total;
    }

    fn centre_of_mass(self) -> Vec2 {
        var weighted = vec2_zero();
        var total_mass = zero_fixed();
        var index: int = 0;
        while (index < self.particles.size()) {
            let ref particle: auto = self.particles[index];
            if (particle.inverse_mass.raw > 0) {
                let mass = particle.inverse_mass.reciprocal();
                weighted = weighted + particle.position * mass;
                total_mass = total_mass + mass;
            }
            index += 1;
        }
        if (total_mass.raw == 0) {
            return vec2_zero();
        }
        return weighted / total_mass;
    }

    fn broken_spring_count(self) -> int {
        var count: int = 0;
        var index: int = 0;
        while (index < self.springs.size()) {
            if (self.springs[index].broken) {
                count += 1;
            }
            index += 1;
        }
        return count;
    }

    var particles: vector(Particle);
    var springs: vector(Spring);
    var damping: Fixed;
    var bounce: Fixed;
    var relaxation_passes: int = 4;
}

fn make_particle_system() -> ParticleSystem {
    var result: ParticleSystem;
    // 0.99 * 65536 = 64880, a light per-step velocity decay.
    result.damping = raw_fixed(64880);
    result.bounce = ratio(-4, 10);
    result.relaxation_passes = DEFAULT_RELAXATION_PASSES;
    return result;
}

// Build a rectangular cloth of width by height particles, pinned along the
// top edge, with structural springs between neighbours and shear springs
// across each cell's diagonals.
fn build_cloth(ref system: ParticleSystem, origin: Vec2,
               columns: int, rows: int, spacing: Fixed,
               stiffness: Fixed) {
    let first_index = system.particles.size();
    var row: int = 0;
    while (row < rows) {
        var column: int = 0;
        while (column < columns) {
            let position = make_vec2(origin.x + spacing * column, origin.y - spacing * row);
            if (row == 0 and (column == 0 or column == columns - 1)) {
                Discard(system.add_pinned(position));
            } else {
                Discard(system.add_particle(position, one_fixed()));
            }
            column += 1;
        }
        row += 1;
    }
    row = 0;
    while (row < rows) {
        var column: int = 0;
        while (column < columns) {
            let here = first_index + row * columns + column;
            if (column + 1 < columns) {
                Discard(system.connect(here, here + 1, stiffness));
            }
            if (row + 1 < rows) {
                Discard(system.connect(here, here + columns, stiffness));
            }
            if (column + 1 < columns and row + 1 < rows) {
                Discard(system.connect(here, here + columns + 1, stiffness / 2));
                Discard(system.connect(here + 1, here + columns, stiffness / 2));
            }
            column += 1;
        }
        row += 1;
    }
}

// A rope of segment_count links hanging from a pinned first particle.
fn build_rope(ref system: ParticleSystem, anchor: Vec2,
              segment_count: int, segment_length: Fixed,
              stiffness: Fixed) -> int {
    let first_index = system.add_pinned(anchor);
    var previous = first_index;
    var index: int = 1;
    while (index <= segment_count) {
        let position = make_vec2(anchor.x, anchor.y - segment_length * index);
        let current = system.add_particle(position, one_fixed());
        Discard(system.connect_at(previous, current, segment_length, stiffness));
        previous = current;
        index += 1;
    }
    return first_index;
}

// --- World ----------------------------------------------------------------
//
// The world owns every body, joint and manifold, and drives the fixed
// timestep loop. The loop is fixed because a variable timestep makes a
// simulation unreproducible and makes the solver's stiffness depend on the
// frame rate, which is how a physics bug becomes a "it only happens on the
// slow machine" bug.

// The simulation runs at 60 steps per second, so one step is 1/60 of a second
// and its raw fixed-point value is 65536 / 60 = 1092.
let DEFAULT_TIMESTEP_RAW: int = 1092;

// Never advance more than this many steps in one update, no matter how far
// behind the accumulator has fallen. Without the cap a machine that cannot
// keep up spirals: each update takes longer, which leaves more to catch up.
let MAX_STEPS_PER_UPDATE: int = 8;

// Substeps per step. Splitting a step into substeps and running the whole
// solve on each is far more effective per unit of work than running more
// solver iterations on one long step.
let DEFAULT_SUBSTEPS: int = 4;

// Gravity: 9.81 * 65536 = 642877, downward.
let GRAVITY_RAW: int = 642877;

// Broad-phase proxy margin: 0.1 of a unit is 6553 raw.
let PROXY_MARGIN_RAW: int = 6553;

// Sleep thresholds. 0.05 units per second linear, 0.02 radians per second
// angular, half a second of stillness.
let SLEEP_LINEAR_RAW: int = 3276;
let SLEEP_ANGULAR_RAW: int = 1310;
let TIME_TO_SLEEP_RAW: int = 32768;

// One frame's worth of counters, for the profiler.
class StepStatistics {

    fn reset(ref self) {
        self.broad_phase_pairs = 0;
        self.narrow_phase_tests = 0;
        self.manifold_count = 0;
        self.contact_point_count = 0;
        self.solver_iterations = 0;
        self.awake_body_count = 0;
        self.position_passes_used = 0;
    }

    // Contact points per manifold, in fixed point. Consistently near two says
    // the scene is mostly face contacts, which is the healthy case.
    fn average_points_per_manifold(self) -> Fixed {
        if (self.manifold_count == 0) {
            return zero_fixed();
        }
        return whole(self.contact_point_count) / whole(self.manifold_count);
    }

    var broad_phase_pairs: int = 0;
    var narrow_phase_tests: int = 0;
    var manifold_count: int = 0;
    var contact_point_count: int = 0;
    var solver_iterations: int = 0;
    var awake_body_count: int = 0;
    var position_passes_used: int = 0;
}

class World {

    // --- Construction -----------------------------------------------------

    fn configure(ref self) {
        self.gravity = make_vec2(zero_fixed(), -raw_fixed(GRAVITY_RAW));
        self.timestep = raw_fixed(DEFAULT_TIMESTEP_RAW);
        self.accumulator = zero_fixed();
        self.substeps = DEFAULT_SUBSTEPS;
        self.velocity_iterations = DEFAULT_VELOCITY_ITERATIONS;
        self.position_iterations = DEFAULT_POSITION_ITERATIONS;
        self.proxy_margin = raw_fixed(PROXY_MARGIN_RAW);
        self.sleep_linear = raw_fixed(SLEEP_LINEAR_RAW);
        self.sleep_angular = raw_fixed(SLEEP_ANGULAR_RAW);
        self.time_to_sleep = raw_fixed(TIME_TO_SLEEP_RAW);
        self.bounds = make_aabb(vec2_from_ints(-500, -500), vec2_from_ints(500, 500));
        self.grid = make_spatial_hash(whole(4));
        self.particles = make_particle_system();
        self.step_index = 0;
    }

    fn add_body(ref self, body: RigidBody) -> int {
        let index = self.bodies.size();
        var stored = move(body);
        stored.identifier = index;
        stored.recompute_mass();
        self.bodies.push_back(move(stored));
        return index;
    }

    fn add_distance_joint(ref self, joint: DistanceJoint) -> int {
        let index = self.distance_joints.size();
        self.distance_joints.push_back(move(joint));
        return index;
    }

    fn add_revolute_joint(ref self, joint: RevoluteJoint) -> int {
        let index = self.revolute_joints.size();
        self.revolute_joints.push_back(move(joint));
        return index;
    }

    fn body_count(self) -> int {
        return self.bodies.size();
    }

    fn joint_count(self) -> int {
        return self.distance_joints.size() + self.revolute_joints.size();
    }

    // --- Broad and narrow phase -------------------------------------------

    fn run_broad_phase(ref self, dt: Fixed) {
        self.grid.rebuild(self.bodies, self.proxy_margin, dt);
        self.candidate_pairs.clear();
        self.grid.collect_pairs(self.bodies, self.candidate_pairs);
        sort_pairs(self.candidate_pairs);
        deduplicate_pairs(self.candidate_pairs);
        self.statistics.broad_phase_pairs = self.candidate_pairs.size();
    }

    // Build this step's manifolds, carrying the accumulated impulses across
    // from last step's manifolds where the contacts match.
    fn run_narrow_phase(ref self) {
        swap(self.manifolds, self.previous_manifolds);
        self.manifolds.clear();
        var index: int = 0;
        while (index < self.candidate_pairs.size()) {
            let ref pair: auto = self.candidate_pairs[index];
            let ref body_a: auto = self.bodies[pair.low];
            let ref body_b: auto = self.bodies[pair.high];
            var manifold = make_manifold(pair.low, pair.high);
            collide(manifold, body_a, body_b);
            if (manifold.is_touching()) {
                // Find last step's manifold for the same pair and warm start
                // from it. A linear scan is fine because the previous list is
                // sorted the same way this one is being built.
                var previous_index: int = 0;
                while (previous_index < self.previous_manifolds.size()) {
                    let ref old: auto = self.previous_manifolds[previous_index];
                    if (old.body_a == manifold.body_a and old.body_b == manifold.body_b) {
                        manifold.warm_start_from(old);
                        previous_index = self.previous_manifolds.size();
                    } else {
                        previous_index += 1;
                    }
                }
                self.statistics.contact_point_count += manifold.points.size();
                self.manifolds.push_back(move(manifold));
            } else {
                Discard(manifold);
            }
            index += 1;
        }
        self.statistics.manifold_count = self.manifolds.size();
    }

    // --- Integration ------------------------------------------------------

    fn integrate_velocities(ref self, dt: Fixed) {
        var index: int = 0;
        while (index < self.bodies.size()) {
            let ref body: auto = self.bodies[index];
            if (body.awake) {
                body.integrate_velocity(self.gravity, dt);
            }
            index += 1;
        }
    }

    fn integrate_positions(ref self, dt: Fixed) {
        var index: int = 0;
        while (index < self.bodies.size()) {
            let ref body: auto = self.bodies[index];
            if (body.awake) {
                body.integrate_position(dt);
            }
            index += 1;
        }
    }

    fn clear_forces(ref self) {
        var index: int = 0;
        while (index < self.bodies.size()) {
            self.bodies[index].clear_forces();
            index += 1;
        }
    }

    fn update_sleeping(ref self, dt: Fixed) {
        var awake_count: int = 0;
        var index: int = 0;
        while (index < self.bodies.size()) {
            let ref body: auto = self.bodies[index];
            body.update_sleep(dt, self.sleep_linear, self.sleep_angular, self.time_to_sleep);
            if (body.awake) {
                awake_count += 1;
            }
            index += 1;
        }
        self.statistics.awake_body_count = awake_count;
    }

    // --- Joints -----------------------------------------------------------

    fn prepare_joints(ref self, dt: Fixed) {
        var index: int = 0;
        while (index < self.distance_joints.size()) {
            self.distance_joints[index].prepare(self.bodies, dt);
            self.distance_joints[index].warm_start(self.bodies);
            index += 1;
        }
        index = 0;
        while (index < self.revolute_joints.size()) {
            self.revolute_joints[index].prepare(self.bodies, dt);
            self.revolute_joints[index].warm_start(self.bodies);
            index += 1;
        }
    }

    fn solve_joint_velocities(ref self, dt: Fixed) {
        var index: int = 0;
        while (index < self.distance_joints.size()) {
            self.distance_joints[index].solve_velocity(self.bodies);
            index += 1;
        }
        index = 0;
        while (index < self.revolute_joints.size()) {
            self.revolute_joints[index].solve_velocity(self.bodies, dt);
            index += 1;
        }
    }

    fn solve_joint_positions(ref self) -> bool {
        var settled: bool = true;
        var index: int = 0;
        while (index < self.distance_joints.size()) {
            if (not self.distance_joints[index].solve_position(self.bodies)) {
                settled = false;
            }
            index += 1;
        }
        index = 0;
        while (index < self.revolute_joints.size()) {
            if (not self.revolute_joints[index].solve_position(self.bodies)) {
                settled = false;
            }
            index += 1;
        }
        return settled;
    }

    // --- One substep ------------------------------------------------------
    //
    // The order matters. Forces are integrated into velocities first so that
    // the contact solver sees the velocity a body will actually have, then
    // the constraints are solved, then the positions are integrated with the
    // corrected velocities, and only then is the remaining penetration
    // pushed out.
    fn substep(ref self, dt: Fixed) {
        self.integrate_velocities(dt);
        self.solver.prepare(self.bodies, self.manifolds, dt);
        self.solver.warm_start(self.bodies, self.manifolds);
        self.prepare_joints(dt);

        var iteration: int = 0;
        while (iteration < self.velocity_iterations) {
            self.solve_joint_velocities(dt);
            self.solver.solve_velocity_once(self.bodies, self.manifolds);
            iteration += 1;
        }
        self.statistics.solver_iterations += self.velocity_iterations;

        self.integrate_positions(dt);

        var pass: int = 0;
        var settled: bool = false;
        while (pass < self.position_iterations and not settled) {
            let deepest = self.solver.solve_position_once(self.bodies, self.manifolds);
            let joints_settled = self.solve_joint_positions();
            if (deepest >= -raw_fixed(PENETRATION_SLOP_RAW) * 3 and joints_settled) {
                settled = true;
            }
            pass += 1;
        }
        self.statistics.position_passes_used += pass;
    }

    // --- One full step ----------------------------------------------------

    fn step(ref self) {
        self.statistics.reset();
        let substep_dt = self.timestep / self.substeps;

        self.run_broad_phase(self.timestep);
        self.run_narrow_phase();

        var index: int = 0;
        while (index < self.substeps) {
            self.substep(substep_dt);
            index += 1;
        }

        self.particles.step(self.gravity, self.timestep, self.bounds);
        self.particles.resolve_body_overlap(self.bodies);

        self.clear_forces();
        self.update_sleeping(self.timestep);
        self.enforce_bounds();
        self.step_index += 1;
    }

    // --- The fixed timestep loop ------------------------------------------
    //
    // Accumulate real elapsed time and consume it in whole steps. Whatever is
    // left over stays in the accumulator for next time, so the simulation
    // clock never drifts from the wall clock by more than one step.
    //
    // Returns how many steps were actually run.
    fn update(ref self, elapsed: Fixed) -> int {
        self.accumulator = self.accumulator + elapsed;
        var steps: int = 0;
        while (self.accumulator >= self.timestep and steps < MAX_STEPS_PER_UPDATE) {
            self.step();
            self.accumulator = self.accumulator - self.timestep;
            steps += 1;
        }
        // If the cap was hit the accumulator is dropped rather than carried,
        // which is the deliberate choice to run slow instead of spiralling.
        if (self.accumulator >= self.timestep * MAX_STEPS_PER_UPDATE) {
            self.accumulator = zero_fixed();
        }
        return steps;
    }

    // The fraction of a step left in the accumulator, which a renderer uses
    // to interpolate between the last two simulated positions.
    fn interpolation_alpha(self) -> Fixed {
        if (self.timestep.raw == 0) {
            return zero_fixed();
        }
        return (self.accumulator / self.timestep).clamped(zero_fixed(), one_fixed());
    }

    // --- World bounds -----------------------------------------------------

    // Anything that leaves the world box is put to sleep where it stands
    // rather than deleted, so that indices stay stable.
    fn enforce_bounds(ref self) {
        var index: int = 0;
        while (index < self.bodies.size()) {
            let ref body: auto = self.bodies[index];
            if (not self.bounds.contains_point(body.frame.position)) {
                body.velocity = vec2_zero();
                body.angular_velocity = zero_fixed();
                body.awake = false;
                body.frame.position = self.bounds.closest_point(body.frame.position);
                body.frame.refresh();
            }
            index += 1;
        }
    }

    // --- Queries ----------------------------------------------------------

    // Every body containing a world point. The broad phase narrows the search
    // to a handful of candidates before the exact test runs.
    fn query_point(self, p: Vec2, ref out: vector(int)) {
        var candidates: vector(int);
        self.grid.query_point(p, candidates);
        var index: int = 0;
        while (index < candidates.size()) {
            let ref body: auto = self.bodies[candidates[index]];
            if (body.shape.contains_point(body.frame, p)) {
                out.push_back(candidates[index]);
            }
            index += 1;
        }
    }

    fn query_region(self, region: AABB, ref out: vector(int)) {
        var candidates: vector(int);
        self.grid.query_box(region, candidates);
        var index: int = 0;
        while (index < candidates.size()) {
            if (self.bodies[candidates[index]].bounds().overlaps(region)) {
                out.push_back(candidates[index]);
            }
            index += 1;
        }
    }

    // Apply an outward impulse to everything within radius of a point, falling
    // off linearly with distance. The classic explosion.
    fn apply_radial_impulse(ref self, centre: Vec2,
                            radius: Fixed, strength: Fixed) {
        var candidates: vector(int);
        self.grid.query_circle(centre, radius, candidates);
        var index: int = 0;
        while (index < candidates.size()) {
            let ref body: auto = self.bodies[candidates[index]];
            let offset = body.world_centre() - centre;
            let distance = offset.length();
            if (distance < radius and distance.raw > LINEAR_EPSILON_RAW) {
                let falloff = one_fixed() - distance / radius;
                let direction = offset / distance;
                body.wake();
                body.apply_impulse_at(direction * strength * falloff, body.world_centre());
            }
            index += 1;
        }
    }

    // --- Energy audit -----------------------------------------------------

    fn total_kinetic_energy(self) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.bodies.size()) {
            total = total + self.bodies[index].kinetic_energy();
            index += 1;
        }
        return total;
    }

    fn total_potential_energy(self) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.bodies.size()) {
            total = total + self.bodies[index].potential_energy(self.gravity, self.bounds.lower.y);
            index += 1;
        }
        return total;
    }

    fn total_energy(self) -> Fixed {
        return self.total_kinetic_energy() + self.total_potential_energy()
            + self.particles.kinetic_energy(self.timestep) + self.particles.spring_energy();
    }

    fn total_linear_momentum(self) -> Vec2 {
        var total = vec2_zero();
        var index: int = 0;
        while (index < self.bodies.size()) {
            total = total + self.bodies[index].linear_momentum();
            index += 1;
        }
        return total;
    }

    fn total_angular_momentum(self) -> Fixed {
        var total = zero_fixed();
        let origin = vec2_zero();
        var index: int = 0;
        while (index < self.bodies.size()) {
            total = total + self.bodies[index].angular_momentum_about(origin);
            index += 1;
        }
        return total;
    }

    fn total_mass(self) -> Fixed {
        var total = zero_fixed();
        var index: int = 0;
        while (index < self.bodies.size()) {
            total = total + self.bodies[index].mass;
            index += 1;
        }
        return total;
    }

    fn centre_of_mass(self) -> Vec2 {
        let mass = self.total_mass();
        if (mass.raw == 0) {
            return vec2_zero();
        }
        var weighted = vec2_zero();
        var index: int = 0;
        while (index < self.bodies.size()) {
            weighted = weighted + self.bodies[index].world_centre() * self.bodies[index].mass;
            index += 1;
        }
        return weighted / mass;
    }

    var bodies: vector(RigidBody);
    var distance_joints: vector(DistanceJoint);
    var revolute_joints: vector(RevoluteJoint);
    var manifolds: vector(Manifold);
    var previous_manifolds: vector(Manifold);
    var candidate_pairs: vector(BodyPair);
    var grid: SpatialHash;
    var solver: ContactSolver;
    var particles: ParticleSystem;
    var statistics: StepStatistics;

    var gravity: Vec2;
    var bounds: AABB;
    var timestep: Fixed;
    var accumulator: Fixed;
    var proxy_margin: Fixed;
    var sleep_linear: Fixed;
    var sleep_angular: Fixed;
    var time_to_sleep: Fixed;
    var substeps: int = 4;
    var velocity_iterations: int = 8;
    var position_iterations: int = 3;
    var step_index: int = 0;
}

fn make_world() -> World {
    var result: World;
    result.configure();
    return result;
}

// --- Scene builder --------------------------------------------------------
//
// Convenience constructors on top of add_body. Every one of them returns the
// body index, because that is what a joint needs and what the checks below
// use to reach back into the world.

fn make_body(kind: BodyKind, shape: Shape, material: Material,
             position: Vec2, angle: Fixed) -> RigidBody {
    var body: RigidBody;
    body.kind = kind;
    body.shape = move(shape);
    body.material = material + make_material(zero_fixed(), zero_fixed(), zero_fixed(), zero_fixed());
    body.frame = make_transform(position, angle);
    body.gravity_scale = one_fixed();
    body.linear_damping = ratio(1, 100);
    body.angular_damping = ratio(5, 100);
    body.max_linear_speed = whole(200);
    body.max_angular_speed = whole(60);
    body.recompute_mass();
    return body;
}

// Materials add componentwise, which is only used by make_body above to force
// a copy of the material without naming every field.
impl Material as Core.AddWith(Material) where .Result = Material {
  fn Op(self, other: Material) -> Material {
      return make_material(self.density + other.density,
                           self.restitution + other.restitution,
                           self.static_friction + other.static_friction,
                           self.dynamic_friction + other.dynamic_friction);
  }
}

fn spawn_circle(ref world: World, position: Vec2,
                radius: Fixed, material: Material) -> int {
    let shape = shape_from_circle(make_circle(vec2_zero(), radius));
    return world.add_body(make_body(BodyKind.Dynamic, shape, material, position, zero_fixed()));
}

fn spawn_box(ref world: World, position: Vec2,
             half_width: Fixed, half_height: Fixed,
             angle: Fixed, material: Material) -> int {
    let shape = shape_box(half_width, half_height);
    return world.add_body(make_body(BodyKind.Dynamic, shape, material, position, angle));
}

fn spawn_polygon(ref world: World, position: Vec2, sides: int,
                 circumradius: Fixed, material: Material) -> int {
    let shape = shape_from_polygon(make_regular_polygon(sides, circumradius));
    return world.add_body(make_body(BodyKind.Dynamic, shape, material, position, zero_fixed()));
}

fn spawn_static_box(ref world: World, position: Vec2,
                    half_width: Fixed, half_height: Fixed,
                    angle: Fixed) -> int {
    let shape = shape_box(half_width, half_height);
    let index = world.add_body(make_body(BodyKind.Static, shape, material_steel(), position, angle));
    let ref body: auto = world.bodies[index];
    body.never_sleeps = true;
    body.freeze();
    return index;
}

// A ground plane, a left wall and a right wall, sized to the world bounds.
fn build_container(ref world: World, thickness: Fixed) {
    let extent = world.bounds.half_extents();
    let centre = world.bounds.centre();
    Discard(spawn_static_box(world,
                             make_vec2(centre.x, world.bounds.lower.y + thickness),
                             extent.x, thickness, zero_fixed()));
    Discard(spawn_static_box(world,
                             make_vec2(world.bounds.lower.x + thickness, centre.y),
                             thickness, extent.y, zero_fixed()));
    Discard(spawn_static_box(world,
                             make_vec2(world.bounds.upper.x - thickness, centre.y),
                             thickness, extent.y, zero_fixed()));
}

// A pyramid of boxes, rows tall, resting on the ground. The classic stability
// test: if warm starting is broken this sinks, and if the position solver is
// too aggressive it explodes.
fn build_pyramid(ref world: World, apex: Vec2, rows: int,
                 half_size: Fixed) {
    let spacing = half_size * 2 + raw_fixed(PENETRATION_SLOP_RAW);
    var row: int = 0;
    while (row < rows) {
        var column: int = 0;
        while (column <= row) {
            let position = make_vec2(
                apex.x + spacing * column - spacing * row / 2,
                apex.y - spacing * row);
            Discard(spawn_box(world, position, half_size, half_size, zero_fixed(), material_wood()));
            column += 1;
        }
        row += 1;
    }
}

// A chain of boxes linked by revolute joints, hanging from a static anchor.
fn build_chain(ref world: World, anchor: Vec2, links: int,
               half_length: Fixed) {
    let anchor_index = spawn_static_box(world, anchor, half_length / 4, half_length / 4, zero_fixed());
    var previous = anchor_index;
    var index: int = 1;
    while (index <= links) {
        let position = make_vec2(anchor.x + half_length * 2 * index, anchor.y);
        let current = spawn_box(world, position, half_length, half_length / 4,
                                zero_fixed(), material_wood());
        Discard(world.add_revolute_joint(make_revolute_joint(
            previous, current,
            make_vec2(half_length, zero_fixed()),
            make_vec2(-half_length, zero_fixed()))));
        previous = current;
        index += 1;
    }
}

// A wheel on a motorised revolute joint, which is the smallest interesting
// test of the motor path.
fn build_motorised_wheel(ref world: World, centre: Vec2,
                         radius: Fixed, speed: Fixed) -> int {
    let hub = spawn_static_box(world, centre, radius / 8, radius / 8, zero_fixed());
    let wheel = spawn_circle(world, centre, radius, material_rubber());
    Discard(world.add_revolute_joint(make_motorised_joint(
        hub, wheel, vec2_zero(), vec2_zero(), speed, whole(500))));
    return wheel;
}

// A ragdoll-ish articulated figure: a torso with four limbs, each limb a box
// on a limited revolute joint.
fn build_articulated_figure(ref world: World, origin: Vec2,
                            scale: Fixed) -> int {
    let torso = spawn_box(world, origin, scale, scale * 2, zero_fixed(), material_wood());
    var limb: int = 0;
    while (limb < 4) {
        let side = limb % 2;
        let level = limb / 2;
        var offset_x = -scale;
        if (side == 1) {
            offset_x = scale;
        }
        let offset_y = scale - scale * 2 * level;
        let position = make_vec2(origin.x + offset_x * 2, origin.y + offset_y);
        let arm = spawn_box(world, position, scale, scale / 2, zero_fixed(), material_wood());
        var joint = make_revolute_joint(torso, arm,
                                        make_vec2(raw_fixed(offset_x.raw), raw_fixed(offset_y.raw)),
                                        make_vec2(-offset_x, zero_fixed()));
        joint.limit_enabled = true;
        joint.lower_limit = -raw_fixed(FIXED_QUARTER_PI_RAW);
        joint.upper_limit = raw_fixed(FIXED_QUARTER_PI_RAW);
        Discard(world.add_revolute_joint(move(joint)));
        limb += 1;
    }
    return torso;
}

// A Newton's cradle: five equal balls hung from five distance joints, just
// touching. Momentum should pass straight through the middle three.
fn build_newtons_cradle(ref world: World, anchor: Vec2,
                        count: int, radius: Fixed,
                        cord_length: Fixed) {
    var index: int = 0;
    while (index < count) {
        let offset_x = radius * 2 * index - radius * count;
        let pivot = make_vec2(anchor.x + offset_x, anchor.y);
        let hub = spawn_static_box(world, pivot, radius / 8, radius / 8, zero_fixed());
        let ball = spawn_circle(world, make_vec2(pivot.x, pivot.y - cord_length),
                                radius, material_steel());
        let ref body: auto = world.bodies[ball];
        body.material.restitution = one_fixed();
        body.linear_damping = zero_fixed();
        body.angular_damping = zero_fixed();
        body.never_sleeps = true;
        Discard(world.add_distance_joint(make_distance_joint(hub, ball, vec2_zero(),
                                                             vec2_zero(), cord_length)));
        index += 1;
    }
}

// The full demo scene: a container, a pyramid, a chain, a wheel, a cradle,
// a hanging cloth and a rope.
fn build_demo_scene() -> World {
    var world = make_world();
    world.bounds = make_aabb(vec2_from_ints(-100, -10), vec2_from_ints(100, 120));
    world.grid.configure(whole(4), DEFAULT_BUCKET_COUNT);

    build_container(world, whole(2));
    build_pyramid(world, make_vec2(zero_fixed(), whole(40)), 8, one_fixed());
    build_chain(world, vec2_from_ints(-60, 100), 10, whole(2));
    Discard(build_motorised_wheel(world, vec2_from_ints(50, 30), whole(6), whole(3)));
    build_newtons_cradle(world, vec2_from_ints(0, 110), 5, one_fixed(), whole(20));
    Discard(build_articulated_figure(world, vec2_from_ints(20, 60), one_fixed()));

    build_cloth(world.particles, vec2_from_ints(-40, 110), 12, 10, one_fixed(), ratio(8, 10));
    Discard(build_rope(world.particles, vec2_from_ints(70, 110), 20, one_fixed(), ratio(9, 10)));

    return world;
}

// A minimal two-body scene, which is what the checks below actually run
// because a two-body result is one a reader can verify by hand.
fn build_pair_scene() -> World {
    var world = make_world();
    world.bounds = make_aabb(vec2_from_ints(-50, -5), vec2_from_ints(50, 50));
    Discard(spawn_static_box(world, vec2_from_ints(0, 0), whole(40), one_fixed(), zero_fixed()));
    Discard(spawn_circle(world, vec2_from_ints(0, 20), one_fixed(), material_rubber()));
    Discard(spawn_box(world, vec2_from_ints(10, 20), one_fixed(), one_fixed(),
                      zero_fixed(), material_wood()));
    return world;
}

// --- Invariant checks -----------------------------------------------------
//
// These are the properties the engine is supposed to have. They are written
// as `assert` where the claim is a runtime one about a particular scene, and
// as `prove` where the claim is meant to hold for every input the verifier
// can reach.
//
// A fixed-point solver is not exactly energy conserving. The three sources of
// loss and gain are:
//
//   * rounding in the multiply and divide, which is unbiased and so shows up
//     as noise rather than drift;
//   * restitution below one, which removes energy deliberately;
//   * position correction, which moves bodies without doing work and so can
//     add energy if it is too aggressive.
//
// So the checks are one-sided: energy may fall freely, but it must never rise
// by more than the tolerance below.

// Allowed energy gain per step, as a fraction of the starting energy:
// 0.02 * 65536 = 1310.
let ENERGY_TOLERANCE_RAW: int = 1310;

// Allowed momentum drift per step in absolute units.
let MOMENTUM_TOLERANCE_RAW: int = 655;

// --- Scalar invariants ----------------------------------------------------

// The properties the fixed-point scalar itself has to have for anything above
// to be trustworthy.
fn check_fixed_invariants() {
    let one = one_fixed();
    let two = whole(2);
    let half = half_fixed();

    // The scale constant is what every conversion is built on.
    Assert(FIXED_ONE == 65536);
    Assert(FIXED_HALF * 2 == FIXED_ONE);
    Assert(FIXED_FRACTION_MASK + 1 == FIXED_ONE);
    Prove(one.raw == FIXED_ONE);

    // Multiplication by one is the identity, and by two is a shift.
    Prove((one * one).raw == FIXED_ONE);
    Prove((two * half).raw == FIXED_ONE);
    Assert(one * two == two);

    // Negation is exact, so it is its own inverse with no rounding drift.
    Prove((-(-one)).raw == one.raw);
    Assert((-one) + one == zero_fixed());

    // Ordering agrees with the underlying integers.
    Prove(half < one);
    Prove(one <= one);
    Prove(two > one);
    Prove(one != two);

    // The reciprocal is accurate to within a couple of units in the last
    // place, which is all sixteen fractional bits can promise.
    let inverse_two = two.reciprocal();
    Assert((inverse_two - half).magnitude() <= raw_fixed(4));
    Assert((two * inverse_two - one).magnitude() <= raw_fixed(8));

    // The square root of a perfect square is exact up to the same tolerance.
    let four = whole(4);
    Assert((four.square_root() - two).magnitude() <= raw_fixed(4));
    Assert((whole(9).square_root() - whole(3)).magnitude() <= raw_fixed(4));
    Prove(whole(0).square_root().raw == 0);

    // Rounding never moves a value by as much as one whole unit.
    let awkward = ratio(1, 3);
    Assert((awkward * whole(3) - one).magnitude() < one);
    Assert(awkward.floor_int() == 0);
    Assert((-awkward).floor_int() == -1);
    Assert((-awkward).ceil_int() == 0);
}

// The trigonometric approximations have to satisfy the Pythagorean identity
// well enough that a rotation matrix stays close to orthonormal.
fn check_trigonometry() {
    let one = one_fixed();
    // 0.01 * 65536 = 655, the accuracy the Bhaskara form promises.
    let tolerance = raw_fixed(655);

    Prove(fixed_sin(zero_fixed()).magnitude() <= tolerance);
    Assert((fixed_cos(zero_fixed()) - one).magnitude() <= tolerance);
    Assert((fixed_sin(raw_fixed(FIXED_HALF_PI_RAW)) - one).magnitude() <= tolerance);
    Assert(fixed_cos(raw_fixed(FIXED_HALF_PI_RAW)).magnitude() <= tolerance);

    var step: int = 0;
    while (step < 16) {
        let angle = tau_fixed() * step / 16;
        let s = fixed_sin(angle);
        let c = fixed_cos(angle);
        // sin^2 + cos^2 == 1, to within the approximation's own error.
        Assert((s * s + c * c - one).magnitude() <= tolerance * 4);
        // A rotation matrix built from that pair has unit determinant.
        let rotation = mat2_rotation(angle);
        Assert((rotation.determinant() - one).magnitude() <= tolerance * 4);
        // Rotating a unit vector preserves its length.
        let rotated = rotation * vec2_unit_x();
        Assert((rotated.length() - one).magnitude() <= tolerance * 4);
        step += 1;
    }
}

// --- Vector and matrix invariants -----------------------------------------

fn check_vector_algebra() {
    let a = vec2_from_ints(3, 4);
    let b = vec2_from_ints(-2, 5);
    let tolerance = raw_fixed(655);

    // The 3-4-5 triangle, which the square root should get exactly right.
    Assert((a.length() - whole(5)).magnitude() <= tolerance);
    Prove(a.length_squared().raw == whole(25).raw);

    // Commutativity of the dot product and anticommutativity of the cross.
    Prove(a.dot(b).raw == b.dot(a).raw);
    Prove(a.cross(b).raw == -b.cross(a).raw);
    Assert(a.cross(a) == zero_fixed());

    // The perpendicular really is perpendicular.
    Prove(a.dot(a.perpendicular()).raw == 0);
    Assert(a.perpendicular().perpendicular() == -a);

    // Addition is commutative and negation is its inverse.
    Assert(a + b == b + a);
    Assert(a + (-a) == vec2_zero());
    Assert((a - b) + b == a);

    // Scaling distributes over addition.
    let s = whole(3);
    Assert((a + b) * s == a * s + b * s);

    // A normalised vector has unit length, and normalising twice changes
    // nothing beyond the rounding of the first pass.
    let unit = a.normalised();
    Assert((unit.length() - one_fixed()).magnitude() <= tolerance);
    Assert((unit.normalised() - unit).manhattan_length() <= tolerance);

    // The componentwise operator agrees with doing it by hand.
    let product = a % b;
    Prove(product.x.raw == (a.x * b.x).raw);
    Prove(product.y.raw == (a.y * b.y).raw);
}

fn check_matrix_algebra() {
    let tolerance = raw_fixed(655);
    let identity = mat2_identity();
    let rotation = mat2_rotation(raw_fixed(FIXED_QUARTER_PI_RAW));
    let v = vec2_from_ints(2, -7);

    Prove(identity.is_identity());
    Assert(identity * v == v);
    Assert(identity * identity == identity);

    // Transposing a rotation inverts it.
    let round_trip = rotation.transposed() * (rotation * v);
    Assert((round_trip - v).manhattan_length() <= tolerance * 4);

    // The inverse really is the inverse.
    let inverse = rotation.inverted();
    let recovered = inverse * (rotation * v);
    Assert((recovered - v).manhattan_length() <= tolerance * 8);

    // Cramer's rule agrees with multiplying by the inverse.
    let solved = rotation.solve(v);
    let by_inverse = inverse * v;
    Assert((solved - by_inverse).manhattan_length() <= tolerance * 8);

    // Matrix addition is componentwise and negation is its inverse.
    Assert(rotation + (-rotation) == mat2_zero());
    Assert((rotation - rotation) == mat2_zero());

    // Composition is associative on rotations, which is what keeps a chain of
    // frames from drifting depending on how it is bracketed.
    let other = mat2_rotation(raw_fixed(FIXED_HALF_PI_RAW));
    let left = (rotation * other) * rotation;
    let right = rotation * (other * rotation);
    Assert((left.col_x - right.col_x).manhattan_length() <= tolerance * 8);
    Assert((left.col_y - right.col_y).manhattan_length() <= tolerance * 8);
}

// --- Geometry invariants --------------------------------------------------

fn check_shape_properties() {
    let tolerance = raw_fixed(6553);
    let box_shape = make_box_polygon(whole(2), whole(3));

    // A 4 by 6 box has area 24 and a centroid at its own origin.
    Assert((box_shape.area() - whole(24)).magnitude() <= tolerance);
    Assert(box_shape.centroid().manhattan_length() <= tolerance);
    Prove(box_shape.is_convex());
    Prove(box_shape.vertices.size() == 4);
    Prove(box_shape.normals.size() == 4);

    // Every normal is a unit vector pointing away from the centre.
    var index: int = 0;
    while (index < box_shape.normals.size()) {
        let normal = box_shape.normals[index];
        Assert((normal.length() - one_fixed()).magnitude() <= raw_fixed(655));
        let midpoint = (box_shape.vertex(index) + box_shape.vertex(index + 1)) / 2;
        Assert(normal.dot(midpoint) > zero_fixed());
        index += 1;
    }

    // A regular polygon of many sides approaches its circumscribed circle.
    let ngon = make_regular_polygon(12, whole(5));
    Prove(ngon.is_convex());
    Assert(ngon.area() < pi_fixed() * whole(25));
    Assert(ngon.area() > pi_fixed() * whole(20));

    // A circle's inertia about its own centre is m r^2 / 2.
    let disc = make_circle(vec2_zero(), whole(2));
    let disc_inertia = disc.inertia_for_mass(whole(10));
    Assert((disc_inertia - whole(20)).magnitude() <= whole(1));
}

fn check_bounds_algebra() {
    let a = make_aabb(vec2_from_ints(0, 0), vec2_from_ints(10, 10));
    let b = make_aabb(vec2_from_ints(5, 5), vec2_from_ints(15, 15));
    let far = make_aabb(vec2_from_ints(100, 100), vec2_from_ints(110, 110));

    Prove(a.overlaps(b));
    Prove(b.overlaps(a));
    Prove(not a.overlaps(far));
    Prove(a.contains_point(vec2_from_ints(5, 5)));
    Prove(not a.contains_point(vec2_from_ints(11, 5)));

    // The union contains both operands and the intersection is contained by
    // both, which is the pair of properties the broad phase relies on.
    let merged = a + b;
    Prove(merged.contains_box(a));
    Prove(merged.contains_box(b));
    let overlap = a * b;
    Prove(a.contains_box(overlap));
    Prove(b.contains_box(overlap));

    // Expanding then translating commutes with translating then expanding.
    let offset = vec2_from_ints(3, -4);
    Assert(a.expanded(one_fixed()).translated(offset) == a.translated(offset).expanded(one_fixed()));

    // Area is monotone under expansion.
    Assert(a.expanded(one_fixed()).area() > a.area());
}

// --- Broad-phase invariants -----------------------------------------------

// Every pair the grid reports must have overlapping proxies, and every pair
// with overlapping proxies that share a cell must be reported. The second
// half is checked here by brute force against the same body list.
fn check_broad_phase(ref world: World) {
    world.run_broad_phase(world.timestep);

    var index: int = 0;
    while (index < world.candidate_pairs.size()) {
        let ref pair: auto = world.candidate_pairs[index];
        Assert(pair.low < pair.high);
        Assert(pair.low >= 0);
        Assert(pair.high < world.bodies.size());
        // Reported pairs really do overlap in the fattened proxies.
        let box_a = world.bodies[pair.low].proxy_bounds(world.proxy_margin, world.timestep);
        let box_b = world.bodies[pair.high].proxy_bounds(world.proxy_margin, world.timestep);
        Assert(box_a.overlaps(box_b));
        // And the pair list has no duplicates, because it was sorted and
        // swept before it got here.
        if (index > 0) {
            Assert(world.candidate_pairs[index - 1] < pair);
        }
        index += 1;
    }

    // Brute force the other direction: anything that actually touches and is
    // allowed to collide must be somewhere in the list.
    var first: int = 0;
    var missed: int = 0;
    while (first < world.bodies.size()) {
        var second = first + 1;
        while (second < world.bodies.size()) {
            let ref a: auto = world.bodies[first];
            let ref b: auto = world.bodies[second];
            if (should_collide(a, b) and a.bounds().overlaps(b.bounds())) {
                let wanted = make_pair(first, second);
                var found: bool = false;
                var scan: int = 0;
                while (scan < world.candidate_pairs.size()) {
                    if (world.candidate_pairs[scan] == wanted) {
                        found = true;
                    }
                    scan += 1;
                }
                if (not found) {
                    missed += 1;
                }
            }
            second += 1;
        }
        first += 1;
    }
    Assert(missed == 0);
}

// --- Narrow-phase invariants ----------------------------------------------

fn check_manifold_shape(manifold: Manifold) {
    // The normal is a unit vector.
    Assert((manifold.normal.length() - one_fixed()).magnitude() <= raw_fixed(655));
    // At most two points, each penetrating rather than separated.
    Assert(manifold.points.size() <= MAX_MANIFOLD_POINTS);
    var index: int = 0;
    while (index < manifold.points.size()) {
        let ref point: auto = manifold.points[index];
        Assert(point.separation <= raw_fixed(PENETRATION_SLOP_RAW));
        // Accumulated normal impulse is never negative: contacts push, they
        // never pull.
        Prove(point.normal_impulse >= zero_fixed());
        // Friction stays inside the Coulomb cone.
        Assert(point.tangent_impulse.magnitude()
            <= manifold.friction * point.normal_impulse + raw_fixed(655));
        index += 1;
    }
}

// Two circles of radius one whose centres are one apart overlap by exactly
// one unit, along the axis joining them.
fn check_circle_circle() {
    let tolerance = raw_fixed(655);
    var manifold = make_manifold(0, 1);
    let circle = make_circle(vec2_zero(), one_fixed());
    let frame_a = make_transform(vec2_from_ints(0, 0), zero_fixed());
    let frame_b = make_transform(vec2_from_ints(1, 0), zero_fixed());
    collide_circle_circle(manifold, circle, frame_a, circle, frame_b);

    Assert(manifold.state == ContactState.Penetrating);
    Assert(manifold.points.size() == 1);
    Assert((manifold.normal.x - one_fixed()).magnitude() <= tolerance);
    Assert(manifold.normal.y.magnitude() <= tolerance);
    Assert((manifold.points[0].separation + one_fixed()).magnitude() <= tolerance);
    check_manifold_shape(manifold);

    // Move them apart and there is no contact at all.
    let far_frame = make_transform(vec2_from_ints(10, 0), zero_fixed());
    collide_circle_circle(manifold, circle, frame_a, circle, far_frame);
    Assert(manifold.state == ContactState.Separated);
    Prove(manifold.points.size() == 0);
}

// Two identical axis-aligned boxes overlapping by one unit should produce a
// two-point face manifold with a vertical normal.
fn check_polygon_polygon() {
    let tolerance = raw_fixed(1310);
    var manifold = make_manifold(0, 1);
    let box_shape = make_box_polygon(whole(2), whole(2));
    let lower = make_transform(vec2_from_ints(0, 0), zero_fixed());
    let upper = make_transform(vec2_from_ints(0, 3), zero_fixed());
    collide_polygon_polygon(manifold, box_shape, lower, box_shape, upper);

    Assert(manifold.state == ContactState.Penetrating);
    Assert(manifold.points.size() == 2);
    Assert(manifold.normal.x.magnitude() <= tolerance);
    Assert((manifold.normal.y.magnitude() - one_fixed()).magnitude() <= tolerance);
    check_manifold_shape(manifold);

    // The two points sit on the shared face, so they have equal depth.
    let depth_difference = manifold.points[0].separation - manifold.points[1].separation;
    Assert(depth_difference.magnitude() <= tolerance);

    // Well separated boxes report nothing.
    let distant = make_transform(vec2_from_ints(0, 40), zero_fixed());
    collide_polygon_polygon(manifold, box_shape, lower, box_shape, distant);
    Assert(manifold.state == ContactState.Separated);
}

// A circle resting on the top face of a box touches at one point with the
// normal pointing out of the face.
fn check_circle_polygon() {
    let tolerance = raw_fixed(1310);
    var manifold = make_manifold(0, 1);
    let circle = make_circle(vec2_zero(), one_fixed());
    let box_shape = make_box_polygon(whole(5), whole(1));
    let circle_frame = make_transform(vec2_from_ints(0, 2), zero_fixed());
    let box_frame = make_transform(vec2_from_ints(0, 0), zero_fixed());
    collide_circle_polygon(manifold, circle, circle_frame, box_shape, box_frame, false);

    Assert(manifold.state == ContactState.Penetrating);
    Assert(manifold.points.size() == 1);
    Assert(manifold.normal.x.magnitude() <= tolerance);
    Assert(manifold.normal.y > zero_fixed());
    check_manifold_shape(manifold);
}

// --- Solver invariants ----------------------------------------------------

// After the velocity solver has run, no contact should still be closing
// faster than the tolerance. This is the property the whole sequential
// impulse scheme exists to establish.
fn check_contacts_are_separating(world: World) {
    let tolerance = raw_fixed(1310);
    var index: int = 0;
    while (index < world.manifolds.size()) {
        let ref manifold: auto = world.manifolds[index];
        let ref body_a: auto = world.bodies[manifold.body_a];
        let ref body_b: auto = world.bodies[manifold.body_b];
        var point_index: int = 0;
        while (point_index < manifold.points.size()) {
            let ref point: auto = manifold.points[point_index];
            let velocity_a = body_a.velocity_at(point.position);
            let velocity_b = body_b.velocity_at(point.position);
            // The long form of the relative normal speed, spelled out over
            // the member chains rather than through a helper, because this is
            // exactly the expression the solver drives to zero.
            let closing = manifold.normal.x * (velocity_b.x - velocity_a.x)
                + manifold.normal.y * (velocity_b.y - velocity_a.y);
            Assert(closing >= -tolerance - point.velocity_bias);
            point_index += 1;
        }
        index += 1;
    }
}

// Momentum is conserved by every impulse the solver applies, because every
// impulse is applied equal and opposite to the two bodies. The only thing
// that changes the total is gravity and the static bodies, so the check runs
// on a scene with neither.
fn check_momentum_conservation() {
    let tolerance = raw_fixed(MOMENTUM_TOLERANCE_RAW);
    var world = make_world();
    world.gravity = vec2_zero();
    world.bounds = make_aabb(vec2_from_ints(-200, -200), vec2_from_ints(200, 200));

    let left = spawn_circle(world, vec2_from_ints(-10, 0), one_fixed(), material_steel());
    let right = spawn_circle(world, vec2_from_ints(10, 0), one_fixed(), material_steel());
    world.bodies[left].set_velocity(vec2_from_ints(5, 0));
    world.bodies[right].set_velocity(vec2_from_ints(-5, 0));
    world.bodies[left].never_sleeps = true;
    world.bodies[right].never_sleeps = true;

    let initial_momentum = world.total_linear_momentum();
    let initial_angular = world.total_angular_momentum();

    var step: int = 0;
    while (step < 120) {
        world.step();
        let momentum = world.total_linear_momentum();
        // Linear momentum drifts only by the rounding of the impulses.
        Assert((momentum.x - initial_momentum.x).magnitude() <= tolerance * (step + 1));
        Assert((momentum.y - initial_momentum.y).magnitude() <= tolerance * (step + 1));
        step += 1;
    }

    let final_angular = world.total_angular_momentum();
    Assert((final_angular - initial_angular).magnitude() <= tolerance * 120);
    Destroy(world);
}

// The central energy check. A closed scene with restitution one and no
// damping may lose energy to rounding and to the position solver, but it must
// never gain more than the tolerance.
fn check_energy_conservation() {
    var world = make_world();
    world.bounds = make_aabb(vec2_from_ints(-60, -5), vec2_from_ints(60, 80));
    build_container(world, one_fixed());

    let ball = spawn_circle(world, vec2_from_ints(0, 40), one_fixed(), material_rubber());
    let ref body: auto = world.bodies[ball];
    body.material.restitution = one_fixed();
    body.linear_damping = zero_fixed();
    body.angular_damping = zero_fixed();
    body.never_sleeps = true;

    let initial_energy = world.total_energy();
    var peak_energy = initial_energy + zero_fixed();
    var minimum_energy = initial_energy + zero_fixed();

    var step: int = 0;
    while (step < 240) {
        let before = world.total_energy();
        world.step();
        let after = world.total_energy();

        // The per-step gain bound. ENERGY_TOLERANCE_RAW is a fraction of the
        // energy present, so a scene at rest is held to a tight absolute
        // bound and an energetic one to a proportional bound.
        let allowed = before.magnitude() * raw_fixed(ENERGY_TOLERANCE_RAW) + one_fixed();
        Assert(after <= before + allowed);

        peak_energy = fixed_max(peak_energy, after);
        minimum_energy = fixed_min(minimum_energy, after);
        check_contacts_are_separating(world);
        step += 1;
    }

    // Over the whole run the energy must not have run away, and a perfectly
    // elastic ball must not have come to a complete stop either.
    let cumulative_allowance = initial_energy.magnitude()
        * raw_fixed(ENERGY_TOLERANCE_RAW) * 8 + whole(10);
    Assert(peak_energy <= initial_energy + cumulative_allowance);
    Assert(minimum_energy >= zero_fixed());
    Prove(peak_energy >= minimum_energy);
    Destroy(world);
}

// Damping and a restitution below one must strictly remove energy, which is
// the other half of the same property.
fn check_energy_dissipation() {
    var world = build_pair_scene();
    let initial_energy = world.total_energy();
    var step: int = 0;
    while (step < 300) {
        world.step();
        step += 1;
    }
    let final_energy = world.total_energy();

    // A scene with friction, damping and restitution below one ends with no
    // more energy than it started with, plus the same rounding allowance.
    let allowance = initial_energy.magnitude() * raw_fixed(ENERGY_TOLERANCE_RAW) + whole(1);
    Assert(final_energy <= initial_energy + allowance);

    // And it should have actually settled: everything asleep or nearly still.
    var index: int = 0;
    while (index < world.bodies.size()) {
        let ref body: auto = world.bodies[index];
        if (body.kind == BodyKind.Dynamic) {
            Assert(not body.awake or body.velocity.length_squared() <= one_fixed());
        }
        index += 1;
    }
    Destroy(world);
}

// --- Particle invariants --------------------------------------------------

// A Verlet particle under no forces moves in a straight line at constant
// speed, and a spring at its rest length applies no correction at all.
fn check_particle_integration() {
    let tolerance = raw_fixed(655);
    let dt = raw_fixed(DEFAULT_TIMESTEP_RAW);
    var system = make_particle_system();
    system.damping = one_fixed();

    let index = system.add_particle(vec2_from_ints(0, 0), one_fixed());
    system.particles[index].set_velocity(vec2_from_ints(10, 0), dt);
    let initial_speed = system.particles[index].velocity(dt).length();

    var step: int = 0;
    while (step < 60) {
        system.integrate(vec2_zero(), dt);
        let speed = system.particles[index].velocity(dt).length();
        Assert((speed - initial_speed).magnitude() <= tolerance * 4);
        // No vertical drift without gravity.
        Assert(system.particles[index].position.y.magnitude() <= tolerance);
        step += 1;
    }

    // After a second at ten units per second it has travelled ten units.
    Assert((system.particles[index].position.x - whole(10)).magnitude() <= one_fixed());
    Destroy(system);
}

fn check_spring_relaxation() {
    let tolerance = raw_fixed(6553);
    var system = make_particle_system();
    system.relaxation_passes = 16;

    let anchor = system.add_pinned(vec2_from_ints(0, 0));
    let free = system.add_particle(vec2_from_ints(10, 0), one_fixed());
    let spring = system.connect_at(anchor, free, whole(5), one_fixed());

    // A pinned particle never moves, whatever the spring does.
    let start = system.particles[anchor].position + vec2_zero();
    system.relax_springs();
    Assert(system.particles[anchor].position == start);

    // And the free end has been pulled to the rest length.
    let length = system.springs[spring].current_length(system.particles);
    Assert((length - whole(5)).magnitude() <= tolerance);
    Assert(system.springs[spring].strain(system.particles).magnitude() <= tolerance);
    Prove(not system.springs[spring].broken);
    Destroy(system);
}

// A cloth pinned at two corners must hang without any particle escaping the
// world box or any spring tearing at rest.
fn check_cloth_stability() {
    var system = make_particle_system();
    let bounds = make_aabb(vec2_from_ints(-100, -100), vec2_from_ints(100, 100));
    build_cloth(system, vec2_from_ints(-10, 50), 8, 8, one_fixed(), ratio(9, 10));
    let gravity = make_vec2(zero_fixed(), -raw_fixed(GRAVITY_RAW));
    let dt = raw_fixed(DEFAULT_TIMESTEP_RAW);

    var step: int = 0;
    while (step < 180) {
        system.step(gravity, dt, bounds);
        step += 1;
    }

    Assert(system.broken_spring_count() == 0);
    var index: int = 0;
    while (index < system.particles.size()) {
        let ref particle: auto = system.particles[index];
        Assert(bounds.contains_point(particle.position));
        // No particle has acquired an absurd speed, which is what a diverging
        // relaxation would look like.
        Assert(particle.velocity(dt).length_squared() <= whole(10000));
        index += 1;
    }

    // The pinned corners are exactly where they were put.
    Prove(system.particles[0].pinned);
    Destroy(system);
}

// --- Joint invariants -----------------------------------------------------

// A rigid distance joint holds its rest length to within the position solver's
// tolerance, and a rope only ever pulls.
fn check_distance_joint() {
    let tolerance = raw_fixed(6553);
    var world = make_world();
    world.bounds = make_aabb(vec2_from_ints(-100, -100), vec2_from_ints(100, 100));

    let anchor = spawn_static_box(world, vec2_from_ints(0, 50), one_fixed(), one_fixed(), zero_fixed());
    let bob = spawn_circle(world, vec2_from_ints(10, 50), one_fixed(), material_steel());
    world.bodies[bob].never_sleeps = true;
    let joint = world.add_distance_joint(
        make_distance_joint(anchor, bob, vec2_zero(), vec2_zero(), whole(10)));

    var step: int = 0;
    while (step < 240) {
        world.step();
        let length = (world.bodies[bob].world_centre()
                      - world.bodies[anchor].world_centre()).length();
        Assert((length - whole(10)).magnitude() <= tolerance * 4);
        Assert(world.distance_joints[joint].status != JointStatus.Broken);
        step += 1;
    }

    // A pendulum swinging from a fixed anchor stays on its circle, so the
    // stretch ratio is one to within the same tolerance.
    let ratio_now = world.distance_joints[joint].stretch_ratio();
    Assert((ratio_now - one_fixed()).magnitude() <= tolerance);
    Destroy(world);
}

// A revolute joint holds its two anchor points together.
fn check_revolute_joint() {
    let tolerance = raw_fixed(6553);
    var world = make_world();
    world.bounds = make_aabb(vec2_from_ints(-100, -100), vec2_from_ints(100, 100));
    build_chain(world, vec2_from_ints(0, 60), 6, one_fixed());

    var step: int = 0;
    while (step < 180) {
        world.step();
        step += 1;
    }

    var index: int = 0;
    while (index < world.revolute_joints.size()) {
        let ref joint: auto = world.revolute_joints[index];
        let world_a = world.bodies[joint.body_a].to_world(joint.local_anchor_a);
        let world_b = world.bodies[joint.body_b].to_world(joint.local_anchor_b);
        // The drift between the two anchors is the joint's whole error.
        Assert((world_b - world_a).manhattan_length() <= tolerance * 8);
        index += 1;
    }
    Destroy(world);
}

// --- Determinism ----------------------------------------------------------

// The same scene stepped the same number of times from the same start must
// produce bit-identical results. This is the property fixed point was chosen
// for, so it is checked exactly rather than with a tolerance.
fn check_determinism() {
    var first = build_pair_scene();
    var second = build_pair_scene();

    var step: int = 0;
    while (step < 120) {
        first.step();
        second.step();
        step += 1;
    }

    Assert(first.bodies.size() == second.bodies.size());
    var index: int = 0;
    while (index < first.bodies.size()) {
        let ref a: auto = first.bodies[index];
        let ref b: auto = second.bodies[index];
        // Exact equality on the raw integers, no tolerance at all.
        Prove(a.frame.position.x.raw == b.frame.position.x.raw);
        Prove(a.frame.position.y.raw == b.frame.position.y.raw);
        Prove(a.frame.angle.raw == b.frame.angle.raw);
        Prove(a.velocity.x.raw == b.velocity.x.raw);
        Prove(a.velocity.y.raw == b.velocity.y.raw);
        Prove(a.angular_velocity.raw == b.angular_velocity.raw);
        index += 1;
    }
    Assert(first.step_index == second.step_index);
    Destroy(first);
    Destroy(second);
}

// --- Timestep invariants --------------------------------------------------

// The accumulator loop must consume whole steps only, must never run more
// than the cap, and must leave less than one step behind.
fn check_fixed_timestep() {
    var world = make_world();
    let dt = world.timestep + zero_fixed();

    // Exactly one step's worth of time runs exactly one step.
    let one_step = world.update(dt);
    Assert(one_step == 1);
    Assert(world.accumulator < dt);

    // Half a step runs nothing and is carried.
    let no_steps = world.update(dt / 2);
    Assert(no_steps == 0);
    Assert(world.accumulator >= zero_fixed());
    Assert(world.accumulator < dt);

    // Three and a half steps run three and carry the half.
    let three_steps = world.update(dt * 3);
    Assert(three_steps == 3);
    Prove(world.accumulator < dt);

    // A huge lag is capped rather than spiralling.
    let capped = world.update(dt * 1000);
    Assert(capped <= MAX_STEPS_PER_UPDATE);
    Assert(world.interpolation_alpha() <= one_fixed());
    Assert(world.interpolation_alpha() >= zero_fixed());
    Destroy(world);
}

// --- Entry point ----------------------------------------------------------

// Run the whole check suite in dependency order: the scalar first, because
// everything above it is meaningless if the scalar is wrong, then the
// algebra, then the geometry, then the pipeline.
fn run_all_checks() {
    check_fixed_invariants();
    check_trigonometry();
    check_vector_algebra();
    check_matrix_algebra();
    check_shape_properties();
    check_bounds_algebra();

    check_circle_circle();
    check_circle_polygon();
    check_polygon_polygon();

    var scene = build_demo_scene();
    check_broad_phase(scene);
    Destroy(scene);

    check_momentum_conservation();
    check_energy_conservation();
    check_energy_dissipation();

    check_particle_integration();
    check_spring_relaxation();
    check_cloth_stability();

    check_distance_joint();
    check_revolute_joint();

    check_determinism();
    check_fixed_timestep();

    println("particle_sim: all checks passed");
    println(to_string(fixed_multiply_count));
    println(to_string(narrow_phase_test_count));
    println(to_string(solver_iteration_count));
}

// Step the demo scene for a while and report what the profiler counters saw.
// This is the benchmark's own workload rather than a correctness check.
fn run_demo(step_count: int) -> int {
    var world = build_demo_scene();
    var step: int = 0;
    while (step < step_count) {
        world.step();
        if (step % 60 == 0) {
            // Once a second, poke the scene so that it never fully settles.
            world.apply_radial_impulse(vec2_from_ints(0, 20), whole(15), whole(40));
        }
        step += 1;
    }
    let total = world.statistics.broad_phase_pairs
        + world.statistics.narrow_phase_tests
        + world.statistics.manifold_count;
    Destroy(world);
    return total;
}


