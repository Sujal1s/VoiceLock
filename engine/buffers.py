import threading
import numpy as np

class Ring:
    """Bounded sample FIFO. Callback callers never wait for a lock."""
    def __init__(self, capacity, channels=1):
        self.data = np.empty((capacity, channels), np.float32)
        self.capacity, self.read, self.write, self.count = capacity, 0, 0, 0
        self.lock = threading.Lock()

    def put(self, x):
        if not self.lock.acquire(False): return False
        try:
            n = len(x)
            if n > self.capacity-self.count: return False
            first = min(n, self.capacity-self.write)
            self.data[self.write:self.write+first] = x[:first]
            self.data[:n-first] = x[first:]
            self.write = (self.write+n) % self.capacity
            self.count += n
            return True
        finally: self.lock.release()

    def take_into(self, out):
        if not self.lock.acquire(False): return 0
        try:
            n = min(len(out), self.count)
            first = min(n, self.capacity-self.read)
            out[:first] = self.data[self.read:self.read+first]
            out[first:n] = self.data[:n-first]
            self.read = (self.read+n) % self.capacity
            self.count -= n
            return n
        finally: self.lock.release()

    def clear(self):
        with self.lock: self.read = self.write = self.count = 0

class Accumulator:
    def __init__(self, size):
        self.size = size
        self.rest = np.empty(0, np.float32)
    def push(self, x):
        x = np.concatenate((self.rest, x))
        end = len(x)//self.size*self.size
        self.rest = x[end:].copy()
        return x[:end].reshape(-1, self.size)

class Adaptive:
    """Three overload observations engage economy monitoring; ten healthy recover.
    Never changes model identity or recurrent-state timing in a running stream.
    """
    def __init__(self):
        self.bad = self.good = 0
        self.economy = False
        self.last_change = -30.
    def update(self, rtf, queue_ms, now):
        overloaded = rtf > .9 or queue_ms > 100
        self.bad = self.bad+1 if overloaded else 0
        self.good = self.good+1 if not overloaded else 0
        if now-self.last_change >= 30:
            if self.bad >= 3 and not self.economy:
                self.economy, self.last_change = True, now
            elif self.good >= 10 and self.economy:
                self.economy, self.last_change = False, now
        return self.economy
