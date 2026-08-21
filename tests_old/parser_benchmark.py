
with open("parser_benchmark.chrg.in", 'r') as f:
    content = f.read()

lineCount = content.count('\n') + 6
with open("parser_benchmark.chrg", 'w') as f:
    f.write("// benchmark\n\n")
    for i in range(1000000 // lineCount):
        f.write("namespace a" + str(i) + ": {\n\n")
        f.write(content);
        f.write("\n\n}\n\n")

