# TIRC: Privacy-Preserving Transformer Inference with Reduced Communication in AIoT

The code has been developed and tested on **Ubuntu 22.04**.


## Requirements

The following dependencies are required:

- `g++` version >= 8
- `cmake`
- `make`
- `libgmp-dev`
- `libmpfr-dev`
- `libssl-dev`
- Microsoft SEAL 4.1.3
- Eigen 3.3


## Project Structure

```text
PrivTI/
├── README.md
├── c_p/                 # Ciphertext-plaintext computation protocols
├── c_c/                 # Ciphertext-ciphertext computation protocols
└── mpc/                 # Secure computation protocols based on MPC
```

The actual directory structure may vary slightly depending on the released version.

## Build and Run

### Build the SCI Library

To compile the SCI library, run:

```bash
cd SCI
mkdir build
cd build
cmake ..
make
```


#### General Test Format

The protocols are typically executed by two parties. Open two terminal windows and run the corresponding binary with different party roles.

For party 1:

```bash
./<test_binary> r=1 [port=port]
```

For party 2:

```bash
./<test_binary> r=2 [port=port]
```

Please replace `<test_binary>` with the actual executable name generated in `build/bin/`.


## Acknowledgements

This repository includes or refers to code from the following external projects:

- [mpc-msri/EzPC](https://github.com/mpc-msri/EzPC) for MPC implementation
- [mpc-msri/EzPC/SCI](https://github.com/mpc-msri/EzPC/tree/master/SCI) for secure computation protocols
- [microsoft/SEAL](https://github.com/microsoft/SEAL) for homomorphic encryption
- [emp-toolkit/emp-tool](https://github.com/emp-toolkit/emp-tool) for cryptographic tools and network I/O

