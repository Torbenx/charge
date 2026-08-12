#include <verify/backend/CompiledMemberString.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <string>

namespace verify::backend {

namespace {
    struct LetterOrder {
        using Letter = CompiledMemberString::Letter;
        bool operator()(const Letter& a, const Letter& b) {
            return std::bit_cast<uint32_t>(a.member) < std::bit_cast<uint32_t>(b.member);
        }
    };
}

CompiledMemberString::CompiledMemberString(std::span<const Member> string) {
    VERIFY(string.size() < sizeof(mask_t) * 8);
    letters.reserve(string.size());
    for (int_t i = 0; i < (int_t)string.size(); i++) {
        Member m = string[i];
        VERIFY(!m.composite());
        letters.push_back({ .member = m, .position = (uint32_t)i });
        if (m.literal())
            literalMask |= positionMask(i);
    }
    std::ranges::sort(letters, LetterOrder());

    // Equal letters are adjacent after the sort, so the duplicates are the runs of length > 1
    for (auto it = letters.begin(); it != letters.end();) {
        mask_t runMask = 0;
        auto runEnd = it;
        for (; runEnd != letters.end() && runEnd->member == it->member; ++runEnd)
            runMask |= positionMask(runEnd->position);
        if (std::popcount(runMask) > 1) {
            // TODO: This should be a verify, duplicate members are not allowed.
            //       See also TODO in Members::compose().
            if (!it->member.literal())
                duplicateVariableMask |= runMask;
        }
        it = runEnd;
    }
}

bool CompiledMemberString::canBeEmpty() const {
    return literalMask == (mask_t)0;
}

bool CompiledMemberString::canBeEqual(Member bLetter) const {
    VERIFY(!bLetter.composite());
    if (bLetter.literal()) {
        bool containsLiteral = literalMask != (mask_t)0;
        if (containsLiteral) {
            // Containing any literal other then bLetter means there can be no model
            for (Letter l : letters) {
                if (l.member.literal() && l.member != bLetter)
                    return false;
            }
            // There is exactly one literal and its bLetter, all variables can be taken to be empty
            return true;
        } else {
            // If there are no literals equality is possible iff there is a variable that can take
            // the value of bLetter. Duplicate variables have to be empty, so they do not qualify.
            bool containsSingleVariable = (~literalMask & ~duplicateVariableMask & sizeMask()) != (mask_t)0;
            return containsSingleVariable;
        }
    } else {
        // bLetter is a variable
        for (Letter l : letters) {
            if (!l.member.literal() && l.member == bLetter) {
                // The variable is shared with the string, so it can taken to be empty.
                return canBeEmpty();
            }
        }
        // Variable not shared the string -> can be equal.
        return true;
    }
}

bool CompiledMemberString::canBeEqual(const CompiledMemberString& b) const {
    const CompiledMemberString& a = *this;
    // Decide if two member expressions can be equal based on Theorem 1 in the SingleMemberEquality document:
    //   There exists a model for s1 and s2 iff all literals shared between s1 and s2
    //   appear in the same order in both strings and the matching (duplicate-free) parts before,
    //   between and after the shared literals meet the criterion of Lemma 2.3 after deleting all
    //   duplicate variables.

    // Phase 1: Detecting shared literals and variables
    mask_t aSharedLiterals = 0;
    mask_t bSharedLiterals = 0;
    // Variables occurring more than once across both strings can to be empty in every model
    // (Lemma 2.2), so they are deleted. The ones duplicated within a single string are already known.
    mask_t aDeletedVariables = a.duplicateVariableMask;
    mask_t bDeletedVariables = b.duplicateVariableMask;

    auto aIt = a.letters.begin();
    auto bIt = b.letters.begin();
    for (;;) {
        if (aIt == a.letters.end() || bIt == b.letters.end())
            break;

        if (LetterOrder()(*aIt, *bIt)) {
            ++aIt;
        } else if (aIt->member == bIt->member) {
            mask_t aMask = positionMask(aIt->position);
            mask_t bMask = positionMask(bIt->position);
            if ((a.literalMask & aMask) != 0) {
                aSharedLiterals |= aMask;
                bSharedLiterals |= bMask;
                // Detect if shared literals are in the same order: Each new shared literal
                // partitions the A and B masks into the literals before and those after it.
                // A shared literal is in the correct relative position to all previous shared
                // literal if and only if both partitions are the same size.
                int aSharedLiteralsBeforeCurrent = std::popcount(aSharedLiterals & (aMask - (mask_t)1));
                int bSharedLiteralsBeforeCurrent = std::popcount(bSharedLiterals & (bMask - (mask_t)1));
                if (aSharedLiteralsBeforeCurrent != bSharedLiteralsBeforeCurrent)
                    return false;
            } else {
                aDeletedVariables |= aMask;
                bDeletedVariables |= bMask;
            }
            ++aIt;
            ++bIt;
        } else {
            ++bIt;
        }
    }

    // Phase 2: Check satisfiability of the duplicate-free parts using Lemma 2.3:
    //   If there are no duplicate literals and variables across s1 and s2 then there exists a model exactly unless:
    mask_t aPreviousInclusiveTailMask = 0;
    mask_t bPreviousInclusiveTailMask = 0;
    mask_t aAllLiterals = a.literalMask;
    mask_t bAllLiterals = b.literalMask;
    mask_t aKeptVariables = ~a.literalMask & ~aDeletedVariables & a.sizeMask();
    mask_t bKeptVariables = ~b.literalMask & ~bDeletedVariables & b.sizeMask();

    for (;;) {
        mask_t aSharedLiteralsMinus1 = aSharedLiterals - (mask_t)1;
        mask_t bSharedLiteralsMinus1 = bSharedLiterals - (mask_t)1;
        // Everything up to and including the lowest shared literal left, or, once none is left,
        // the whole string (as the all ones mask, minus its top bit taken by the shift below)
        mask_t aInclusiveTailMask = aSharedLiterals ^ aSharedLiteralsMinus1;
        mask_t bInclusiveTailMask = bSharedLiterals ^ bSharedLiteralsMinus1;
        mask_t aExclusiveMask = (aInclusiveTailMask >> 1) & ~aPreviousInclusiveTailMask;
        mask_t bExclusiveMask = (bInclusiveTailMask >> 1) & ~bPreviousInclusiveTailMask;

        mask_t aLiteralsInRange = aAllLiterals & aExclusiveMask;
        mask_t bLiteralsInRange = bAllLiterals & bExclusiveMask;
        mask_t aVariablesInRange = aKeptVariables & aExclusiveMask;
        mask_t bVariablesInRange = bKeptVariables & bExclusiveMask;

        //   1. s1 and s2 both start with a literal
        //   2. s1 and s2 both end with a literal
        // A tie is only possible when the expression is empty in which case 'startsWith' and 'endsWith' should be false
        bool aStartsWithLiteral = std::countr_zero(aLiteralsInRange) < std::countr_zero(aVariablesInRange);
        bool bStartsWithLiteral = std::countr_zero(bLiteralsInRange) < std::countr_zero(bVariablesInRange);
        bool aEndsWithLiteral = aLiteralsInRange > aVariablesInRange;
        bool bEndsWithLiteral = bLiteralsInRange > bVariablesInRange;
        if (aStartsWithLiteral & bStartsWithLiteral)
            return false;
        if (aEndsWithLiteral & bEndsWithLiteral)
            return false;

        //   3. s1 contains no variables and s2 contains a literal
        //   4. s2 contains no variables and s1 contains a literal
        bool aContainsLiteral = aLiteralsInRange != (mask_t)0;
        bool bContainsLiteral = bLiteralsInRange != (mask_t)0;
        bool aContainsVariable = aVariablesInRange != (mask_t)0;
        bool bContainsVariable = bVariablesInRange != (mask_t)0;
        if (aContainsLiteral & !bContainsVariable)
            return false;
        if (bContainsLiteral & !aContainsVariable)
            return false;

        // The two masks always hold the same number of shared literals, so they run out together
        if (aSharedLiterals == (mask_t)0)
            return true;
        aPreviousInclusiveTailMask = aInclusiveTailMask;
        bPreviousInclusiveTailMask = bInclusiveTailMask;
        aSharedLiterals &= aSharedLiteralsMinus1;
        bSharedLiterals &= bSharedLiteralsMinus1;
    }
}

namespace {

    using String = std::vector<Member>;

    Member literal(uint32_t id) { return Member(TheoryId::MemberLiterals, id); }
    Member variable(uint32_t id) { return Member(TheoryId::AuxMemberVariables, id); }

    std::string toString(std::span<const Member> string) {
        if (string.empty())
            return "<empty>";
        std::string result;
        for (Member m : string)
            result += std::format("{}{}", m.literal() ? 'l' : 'v', m.id());
        return result;
    }

    bool contains(std::span<const Member> string, Member m) {
        return std::ranges::find(string, m) != string.end();
    }

    bool hasDuplicate(std::span<const Member> string) {
        for (int_t i = 0; i < (int_t)string.size(); i++) {
            if (contains(string.first(i), string[i]))
                return true;
        }
        return false;
    }

    //! All strings over \p alphabet that use each letter at most once, including the empty one
    std::vector<String> duplicateFreeStrings(std::span<const Member> alphabet) {
        std::vector<String> result { String {} };
        for (int_t i = 0; i < (int_t)result.size(); i++) {
            for (Member m : alphabet) {
                if (contains(result[i], m))
                    continue;
                String extended = result[i];
                extended.push_back(m);
                result.push_back(std::move(extended));
            }
        }
        return result;
    }

    //! Reference implementation of canBeEqual() searching all models by brute force
    /*!
    A model is a substitution of the variables making the two strings equal without producing a
    duplicate literal. By Lemma 1.1 of the SingleMemberEquality document it is enough to search
    the substitutions over the literals appearing in \p a and \p b, and being duplicate free
    restricts every image to a string using each of those literals at most once.
    */
    bool hasModel(std::span<const Member> a, std::span<const Member> b) {
        String literals;
        String variables;
        for (std::span<const Member> string : { a, b }) {
            for (Member m : string) {
                String& seen = m.literal() ? literals : variables;
                if (!contains(seen, m))
                    seen.push_back(m);
            }
        }

        std::vector<String> images = duplicateFreeStrings(literals);
        std::vector<int_t> choice(variables.size(), 0);

        auto substitute = [&](std::span<const Member> string) {
            String result;
            for (Member m : string) {
                if (m.literal()) {
                    result.push_back(m);
                } else {
                    const String& image = images[choice[std::ranges::find(variables, m) - variables.begin()]];
                    result.insert(result.end(), image.begin(), image.end());
                }
            }
            return result;
        };

        for (;;) {
            String aImage = substitute(a);
            if (aImage == substitute(b) && !hasDuplicate(aImage))
                return true;

            // Advance the odometer over the images of all variables
            int_t digit = 0;
            for (; digit < (int_t)choice.size(); digit++) {
                if (++choice[digit] < (int_t)images.size())
                    break;
                choice[digit] = 0;
            }
            if (digit == (int_t)choice.size())
                return false;
        }
    }

    //! All strings of at most \p maxLength letters over the given alphabet
    /*!
    Strings repeating a literal are left out, as they have no proper substitution at all and are
    therefore outside of the domain of CompiledMemberString.
    */
    std::vector<String> allStrings(int_t literalCount, int_t variableCount, int_t maxLength) {
        String alphabet;
        for (int_t i = 0; i < literalCount; i++)
            alphabet.push_back(literal(i));
        for (int_t i = 0; i < variableCount; i++)
            alphabet.push_back(variable(i));

        std::vector<String> result { String {} };
        for (int_t i = 0; i < (int_t)result.size(); i++) {
            if ((int_t)result[i].size() == maxLength)
                continue;
            for (Member m : alphabet) {
                if (m.literal() && contains(result[i], m))
                    continue;
                String extended = result[i];
                extended.push_back(m);
                result.push_back(std::move(extended));
            }
        }
        return result;
    }

    //! Compare canBeEqual() against hasModel() for every pair of strings over the given alphabet
    void expectModelsForAllPairs(int_t literalCount, int_t variableCount, int_t maxLength) {
        std::vector<String> strings = allStrings(literalCount, variableCount, maxLength);
        std::vector<std::string> mismatches;
        auto expect = [&](bool result, bool expected, std::span<const Member> a, std::span<const Member> b, std::string_view what) {
            if (result != expected)
                mismatches.push_back(std::format("{}('{}', '{}') == {}", what, toString(a), toString(b), result));
        };

        for (const String& a : strings) {
            CompiledMemberString compiledA(a);
            for (const String& b : strings) {
                bool expected = hasModel(a, b);
                expect(compiledA.canBeEqual(CompiledMemberString(b)), expected, a, b, "canBeEqual");
                if (b.size() == 1)
                    expect(compiledA.canBeEqual(b.front()), expected, a, b, "canBeEqual(letter)");
                if (b.empty())
                    expect(compiledA.canBeEmpty(), expected, a, b, "canBeEmpty");
            }
        }

        std::string report;
        for (const std::string& mismatch : std::span(mismatches).first(std::min<size_t>(mismatches.size(), 10)))
            report += "\n  " + mismatch;
        EXPECT_TRUE(mismatches.empty()) << mismatches.size() << " of " << strings.size() * strings.size()
                                        << " pairs disagree with the brute force search:" << report;
    }

}

TEST(VerifyBackend, CompiledMemberStringCompilation) {
    String emptyString;
    CompiledMemberString empty(emptyString);
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(empty.sizeMask(), (uint64_t)0);
    EXPECT_EQ(empty.literalMask, (uint64_t)0);
    EXPECT_TRUE(empty.canBeEmpty());

    String string { variable(0), literal(1), variable(0), literal(0) };
    CompiledMemberString compiled(string);
    EXPECT_EQ(compiled.size(), 4);
    EXPECT_EQ(compiled.sizeMask(), (uint64_t)0b1111);
    EXPECT_EQ(compiled.literalMask, (uint64_t)0b1010);
    // Both occurrences of the variable are duplicates, no model can give them a value
    EXPECT_EQ(compiled.duplicateVariableMask, (uint64_t)0b0101);
    EXPECT_FALSE(compiled.canBeEmpty());

    // The letters are the string sorted by member, each of them remembering its position
    ASSERT_EQ((int_t)compiled.letters.size(), compiled.size());
    uint64_t seenPositions = 0;
    for (auto [member, position] : compiled.letters) {
        EXPECT_EQ(member, string[position]);
        seenPositions |= CompiledMemberString::positionMask(position);
    }
    EXPECT_EQ(seenPositions, compiled.sizeMask());
    EXPECT_TRUE(std::ranges::is_sorted(compiled.letters, LetterOrder()));
}

TEST(VerifyBackend, CompiledMemberStringEqualityWithALiteral) {
    Member l0 = literal(0);
    Member l1 = literal(1);
    Member v0 = variable(0);
    auto canBeEqual = [](String string, Member letter) { return CompiledMemberString(string).canBeEqual(letter); };

    // A single literal is only reachable through a string whose only literal is that one
    EXPECT_TRUE(canBeEqual({ l0 }, l0));
    EXPECT_FALSE(canBeEqual({ l1 }, l0));
    EXPECT_TRUE(canBeEqual({ v0, l0 }, l0));
    EXPECT_FALSE(canBeEqual({ l0, l1 }, l0));

    // Without a literal a single variable has to take the value, but a duplicated one is empty
    EXPECT_TRUE(canBeEqual({ v0 }, l0));
    EXPECT_FALSE(canBeEqual({}, l0));
    EXPECT_FALSE(canBeEqual({ v0, v0 }, l0));
}

TEST(VerifyBackend, CompiledMemberStringEqualityWithAVariable) {
    Member l0 = literal(0);
    Member v0 = variable(0);
    Member v1 = variable(1);
    auto canBeEqual = [](String string, Member letter) { return CompiledMemberString(string).canBeEqual(letter); };

    // A fresh variable can take the value of the whole string
    EXPECT_TRUE(canBeEqual({}, v0));
    EXPECT_TRUE(canBeEqual({ l0 }, v0));
    EXPECT_TRUE(canBeEqual({ v1, l0 }, v0));

    // A shared variable cancels out, leaving the rest of the string to be empty
    EXPECT_TRUE(canBeEqual({ v0, v1 }, v0));
    EXPECT_FALSE(canBeEqual({ v0, l0 }, v0));
}

TEST(VerifyBackend, CompiledMemberStringEqualityOfTwoStrings) {
    Member l0 = literal(0);
    Member l1 = literal(1);
    Member v0 = variable(0);
    Member v1 = variable(1);
    auto canBeEqual = [](String a, String b) { return CompiledMemberString(a).canBeEqual(CompiledMemberString(b)); };

    // Literals only ever match themselves, so distinct ones need a variable to absorb them
    EXPECT_TRUE(canBeEqual({ l0 }, { l0 }));
    EXPECT_FALSE(canBeEqual({ l0 }, { l1 }));
    EXPECT_TRUE(canBeEqual({ l0, v0 }, { v1, l1 }));
    EXPECT_FALSE(canBeEqual({ l0, v0 }, { l1, v1 }));
    EXPECT_FALSE(canBeEqual({ v0, l0 }, { v1, l1 }));

    // Shared literals have to appear in the same order in both strings
    EXPECT_TRUE(canBeEqual({ l0, v0, l1 }, { l0, v1, l1 }));
    EXPECT_FALSE(canBeEqual({ l0, v0, l1 }, { l1, v1, l0 }));

    // The parts between the shared literals are checked on their own
    EXPECT_TRUE(canBeEqual({ v0, l0 }, { l0, v1 }));
    EXPECT_FALSE(canBeEqual({ l1, l0 }, { l0, v1 }));
    EXPECT_FALSE(canBeEqual({ l0, l1 }, { v1, l0 }));

    // A variable shared between both strings has to be empty
    EXPECT_TRUE(canBeEqual({ v0, l0 }, { l0, v0 }));
    EXPECT_FALSE(canBeEqual({ v0, l0 }, { l0, v0, l1 }));
}

TEST(VerifyBackend, CompiledMemberStringEqualityAgainstBruteForce) {
    // Enough letters to place two shared literals with parts before, between and after them
    expectModelsForAllPairs(3, 2, 3);
    // Longer strings, which need duplicated variables to be interesting at two literals
    expectModelsForAllPairs(2, 2, 4);
}

}
