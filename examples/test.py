
import sys
sys.path.append("../build/lib.linux-x86_64-3.6")
import time

from circularbuffer import CircularBuffer

ba = bytearray(10)

b = CircularBuffer(48)
print(b)
print(b.write(b"Hello"))
print(b)
print(b.peek(3))
print(b.drop(2))
print(b)
print(b.peek(4))
print(b)
print(b.read(4))
print(b)
print(b.read(4))
print(b)
print(b.write(b"Hello"))
print(b)
print(b.clear())
print(b)
print("writemsg", b.writemsg(b"Hello"))
print(b)
print(b.peek(10))
print("peekmsg", b.peekmsg())
print(b)
print("readmsg", b.readmsg())
print(b)
print("readmsg", b.readmsg())
print(b)
print(b.writemsg(b"Hello"))
print(b)
print(b.readmsg_into(ba))
print(ba)
print(b)

for _ in range(10):
  b.write(b"Hello")
  print(b.read(48))

for i in range(10):
  b.writemsg(b"Hello%d" % i)
  print(b.readmsg())

t1 = time.time()
for _ in range(1000000):
  b.write(b"Hello")
  b.read(48)
print(time.time() - t1)

t1 = time.time()
for _ in range(1000000):
  b.writemsg(b"Hello")
  b.readmsg()
print(time.time() - t1)

