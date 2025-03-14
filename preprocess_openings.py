books = []
files = ["Titans.bin"]

def read_book(f):
	book = {}
	while True:
		hash = f.read(8)
		if not hash:
			break
		hash = int.from_bytes(hash, "big")
		move = f.read(2)
		if not move:
			break
		move = int.from_bytes(move, "big")
		move_from = move >> 6 & 0x3F
		move_to = move & 0x3F
		weight = f.read(2)
		if not weight:
			break
		weight = int.from_bytes(weight, "big")
		learning = f.read(2)
		if not learning:
			break
		padding = f.read(2)
		if not padding:
			break
		if hash not in book:
			book[hash] = []
		book[hash].append({"from": move_from, "to": move_to, "weight": weight})
	return book

for file in files:
	with open(file, "rb") as f:
		books.append(read_book(f))

out = {}
for e in books[0]:
	hash, moves = e, books[0][e]
	move = sorted(moves, key=lambda x: x["weight"], reverse=True)[0]
	out[hash] = move
	# if hash == 0x463b96181691fc9c:
	# 	print(hash, "->", move)
	# 	exit()

with open("out.bin", "wb") as f:
	for h in out:
		hash, move = h, out[h]
		f.write(hash.to_bytes(8, "little"))
		f.write(move["from"].to_bytes(4, "little"))
		f.write(move["to"].to_bytes(4, "little"))

# print(books[0][0x463b96181691fc9c])

print(out[0x830eb9b20758d1de])

print(len(out), "entries")
