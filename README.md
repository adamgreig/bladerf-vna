# bladerf-vna
A poor man's VNA using a BladeRF to measure the frequency response of things.

This is some hastily written C so all the usual caveats apply.

Note: not a real VNA. Just measures S21. At best, the shape of S21 against 
frequency. Don't expect the dB to mean anything.

## Operation

```sh
vi main.c # edit f_low, f_high, f_step, tx_gain, rx_gain at top of main()
make
./vna
vi results.txt # paste in the results output
python plot.py results.txt
```

## Theory / Thoughts

This software configures the BladeRF to a reasonably low bandwidth (1.5MHz) and 
sample rate (4Ms/s) and then steps the transmit side through the frequency band 
of interest, transmitting a constant tone at DC (i.e., at the LO frequency). 
The receiver is tuned 1kHz below the transmitter and we then sum up the average 
$I^2+Q^2$ received over all the buffers. Because something is slightly wrong 
somewhere, some of the final buffers aren't populated when they get read, so we 
just don't read the last third of the buffers. YMMV.

Possible improvements include transmitting white noise wideband and just taking 
the FFT of the result. It would at least be quicker.

## Examples

![habamp](https://github.com/adamgreig/bladerf-vna/raw/master/habamp.png "HABAMP")
![m2pa](https://github.com/adamgreig/bladerf-vna/raw/master/m2pa.png "M2PA")
