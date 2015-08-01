import sys
import numpy as np
import matplotlib.pyplot as plt

if len(sys.argv) != 2:
    print("Usage: {} <text file>".format(sys.argv[0]))
    sys.exit(0)

data = np.loadtxt(sys.argv[1])
freqs = data[:, 0]
pwrs = 10 * np.log10(data[:, 1] / np.max(data[:, 1]))
plt.plot(data[:, 0], pwrs, 'x-')
plt.xlabel("Frequency (Hz)")
plt.ylabel("Normalised Power (dBFS)")
plt.grid()
plt.title("M2PA Spectral Response")
plt.savefig("m2pa.png")
plt.show()
