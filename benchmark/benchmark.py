
files = [
    'std.chrg',
    'b_tree.chrg',
    'bignum.chrg',
    'bytecode_vm.chrg',
    'chess_movegen.chrg',
    'compression.chrg',
    'expression_language.chrg',
    'graph_algorithms.chrg',
    'json.chrg',
    'particle_sim.chrg',
    'red_black_tree.chrg',
    'reference.chrg',
    'regex_engine.chrg',
    'slab_allocator.chrg',
    'trie_autocomplete.chrg',
    'utf8_codec.chrg'
]

with open("benchmark.chrg", 'w') as fw:
    for file in files:
        with open(file, 'r') as fr:
            fw.write(fr.read())
