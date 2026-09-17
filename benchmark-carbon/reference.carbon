// Open-addressing hash map with Robin Hood probing.
namespace containers;

choice SlotState {
    Empty,
    Occupied,
    Tombstone,
}

let DEFAULT_CAPACITY: int = 16;
let MAX_LOAD_PERCENT: int = 75;

class Slot(K: type, V: type)
{
    var key: K;
    var value: V;
    var hash: int = 0;
    var distance: int = 0;
    var state: SlotState = SlotState.Empty;
}

class HashMap(K: type, V: type)
{

    fn size(self) -> int {
        return self.count;
    }

    fn empty(self) -> bool {
        return self.count == 0;
    }

    fn slot_index(self, h: int) -> int {
        return h & (self.slots.size() - 1);
    }

    fn should_grow(self) -> bool {
        let limit = self.slots.size() * MAX_LOAD_PERCENT;
        return self.count * 100 >= limit;
    }

    fn find(ref self, key: K) -> ref V {
        let h = hash_of(key);
        var index = self.slot_index(h);
        var probe: int = 0;
        while (true) {
            let ref slot: auto = self.slots[index];
            if (slot.state == SlotState.Empty) {
                break;
            }
            if (slot.state == SlotState.Occupied and slot.hash == h) {
                if (slot.key == *key) {
                    return slot.value;
                }
            }
            if (probe > slot.distance) {
                break;
            }
            index = self.slot_index(index + 1);
            probe += 1;
        }
        return self.missing;
    }

    fn insert(ref self, key: K, value: V) -> bool {
        if (self.should_grow()) {
            self.rehash(self.slots.size() * 2);
        }
        let h = hash_of(key);
        var index = self.slot_index(h);
        var distance: int = 0;
        var pending = Slot(K, V)({.key = key, .value = value, .hash = h});
        while (true) {
            let ref slot: auto = self.slots[index];
            if (slot.state != SlotState.Occupied) {
                slot.key = pending.key;
                slot.value = pending.value;
                slot.hash = pending.hash;
                slot.distance = distance;
                slot.state = SlotState.Occupied;
                self.count += 1;
                return true;
            }
            if (slot.hash == h and slot.key == pending.key) {
                slot.value = pending.value;
                return false;
            }
            if (slot.distance < distance) {
                swap(slot.key, pending.key);
                swap(slot.value, pending.value);
                swap(slot.distance, distance);
            }
            index = self.slot_index(index + 1);
            distance += 1;
        }
    }

    fn erase(ref self, key: K) -> bool {
        var index = self.slot_index(hash_of(key));
        var probe: int = 0;
        while (probe < self.slots.size()) {
            let ref slot: auto = self.slots[index];
            if (slot.state == SlotState.Empty) {
                return false;
            }
            if (slot.state == SlotState.Occupied and slot.key == *key) {
                slot.state = SlotState.Tombstone;
                self.count -= 1;
                return true;
            }
            index = self.slot_index(index + 1);
            probe += 1;
        }
        return false;
    }

    fn rehash(ref self, capacity: int) {
        var old = move(self.slots);
        self.slots = vector(Slot(K, V))(capacity);
        self.count = 0;
        for (entry in old) {
            if (entry.state == SlotState.Occupied) {
                self.insert(entry.key, entry.value);
            } else {
                Discard(entry);
            }
        }
        Destroy(old);
    }

    fn clear(ref self) {
        for (slot in self.slots) {
            slot.state = SlotState.Empty;
            slot.distance = 0;
        }
        self.count = 0;
    }

    var slots: vector(Slot(K, V));
    var count: int = 0;
    var missing: V;
}


