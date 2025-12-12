#! /bin/bash

echo "Building examples..."

# Probe PSE module
sudo modprobe pse
lsmod | grep pse

# Build examples
make clean
make all

# Check PSE firmware version
sudo ./version

# Load vcan kernel module and bring it online
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
ip addr | grep “can“

string=$(lsmod | grep pse)
if [[ $string =~ "pse" ]]
then
echo "Examples built successfully!"
else
echo "Failed to open the pse device file"
fi


