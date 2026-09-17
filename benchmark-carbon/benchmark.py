
files = [
    'std.carbon',
    'b_tree.carbon',
    'bignum.carbon',
    'bytecode_vm.carbon',
    'chess_movegen.carbon',
    'compression.carbon',
    'expression_language.carbon',
    'graph_algorithms.carbon',
    'json.carbon',
    'particle_sim.carbon',
    'red_black_tree.carbon',
    'reference.carbon',
    'regex_engine.carbon',
    'slab_allocator.carbon',
    'trie_autocomplete.carbon',
    'utf8_codec.carbon'
]

with open("benchmark.carbon", 'w') as fw:
    for file in files:
        with open(file, 'r') as fr:
            fw.write(fr.read())
