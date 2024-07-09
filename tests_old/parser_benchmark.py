
with open("parser_benchmark.chrg.in", 'r') as f:
    content = f.read()

with open("parser_benchmark.chrg", 'w') as f:
    f.write("// benchmark\n\n")
    for i in range(2223):
        f.write("namespace a" + str(i) + ": {\n\n")
        f.write(content);
        f.write("\n\n}\n\n")

