// Shared standard-library surface for the benchmark corpus.
//
// This is a stub: bodies are empty and it is not expected to pass semantic
// analysis. Its purpose is to give every generated benchmark file one
// consistent set of names to call, so the corpus reads like a single codebase.

namespace std;

// --- Core scalar aliases -------------------------------------------------

class int { }
class bits { }
class byte { }
// bool is built in
// char is built in

let true_value: bool;
let false_value: bool;

// --- Utility -------------------------------------------------------------

fn swap[T: type](ref a: T, ref b: T) { }
fn move[T: type](ref value: T) -> T { }
fn min[T: type](a: T, b: T) -> ref const T { }
fn max[T: type](a: T, b: T) -> ref const T { }
fn hash_of[T: type](value: T) -> int { }
fn abs[T: type](value: T) -> T { }

fn range(begin: int, end: int) -> Range { }

class Range {
    var begin: int = 0;
    var end: int = 0;
}

// --- vector --------------------------------------------------------------

class vector(T: type)
{
    fn size(self) -> int { }
    fn empty(self) -> bool { }
    fn capacity(self) -> int { }
    fn reserve(ref self, n: int) { }
    fn resize(ref self, n: int) { }
    fn clear(ref self) { }
    fn push_back(ref self, value: T) { }
    fn pop_back(ref self) { }
    fn front(ref self) -> ref T { }
    fn back(ref self) -> ref T { }
    fn at(ref self, index: int) -> ref T { }
    fn insert(ref self, index: int, value: T) { }
    fn erase(ref self, index: int) { }

    var data: T;
    var count: int = 0;
}

// --- string --------------------------------------------------------------

class string {
    fn size(self) -> int { }
    fn empty(self) -> bool { }
    fn at(self, index: int) -> char { }
    fn append(ref self, other: string) { }
    fn push_back(ref self, c: char) { }
    fn substr(self, begin: int, length: int) -> string { }
    fn find(self, needle: string) -> int { }
    fn compare(self, other: string) -> int { }
    fn clear(ref self) { }

    var data: char;
    var count: int = 0;
}

fn to_string(value: int) -> string { }
fn parse_int(text: string) -> int { }

// --- map / set -----------------------------------------------------------

class map(K: type, V: type)
{
    fn size(self) -> int { }
    fn empty(self) -> bool { }
    fn contains(self, key: K) -> bool { }
    fn insert(ref self, key: K, value: V) -> bool { }
    fn erase(ref self, key: K) -> bool { }
    fn at(ref self, key: K) -> ref V { }
    fn clear(ref self) { }

    var count: int = 0;
}

class set(T: type)
{
    fn size(self) -> int { }
    fn contains(self, value: T) -> bool { }
    fn insert(ref self, value: T) -> bool { }
    fn erase(ref self, value: T) -> bool { }
    fn clear(ref self) { }

    var count: int = 0;
}

// --- pair ----------------------------------------------------------------

class pair(A: type, B: type)
{
    var first: A;
    var second: B;
}

// --- memory --------------------------------------------------------------

class unique_ptr(T: type)
{
    fn get(ref self) -> ref T { }
    fn valid(self) -> bool { }
    fn reset(ref self) { }

    var target: T;
}

fn make_unique[T: type](value: T) -> unique_ptr(T) { }

// --- io ------------------------------------------------------------------

fn print(text: string) { }
fn println(text: string) { }
fn panic(message: string) { }


