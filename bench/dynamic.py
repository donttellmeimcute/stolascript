words = ["sol", "luna", "mar", "tierra", "fuego", "agua", "aire", "piedra"]
counts = {}
for i in range(300000):
    w = words[(i * 7 + i // 3) % 8]
    counts[w] = counts.get(w, 0) + 1
total = 0
for k in counts.keys():
    total += counts[k]
parts = []
for i in range(50000):
    parts.append(str(i % 97))
s = ",".join(parts)
print(total, len(s), sorted(counts.keys())[0])
