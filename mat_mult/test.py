import numpy as np

DIM = 64

a = []
for i in range(DIM):
	a.append(list(range(DIM*i, DIM*i + DIM)))

b = []
for i in range(DIM):
	b.append(list(range(64+DIM*i, 64 + DIM*i + DIM)))

a = np.array(a)
b = np.array(b)
c = np.matmul(a, a)
print(c)
