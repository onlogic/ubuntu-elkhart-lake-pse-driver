#! /bin/bash

echo "Installing..."
# Prepare the build environment
sudo apt-get update
sudo apt-get install wget build-essential
sudo apt-get install build-essential flex bison libssl-dev libelf-dev
sudo apt --fix-broken install

# Download the kernel headers for the current kernel
string=$(sudo apt-get install linux-headers-$(uname -r))

# Install DKMS package
sudo apt-get -y install dkms

sudo rm -rf /usr/src/pse-1.3
sudo mkdir /usr/src/pse-1.3
sudo cp -r ./src /usr/src/pse-1.3
sudo cp ./dkms.conf /usr/src/pse-1.3

# Build PSE v1.3 with DKMS
sudo dkms remove pse/1.3 --all
sudo dkms add -m pse -v 1.3
sudo dkms build -m pse -v 1.3
sudo dkms install -m pse -v 1.3

# Check PSE v1.3 has been built and installed correctly
sudo dkms status

# Take AUTOINSTALL into effect
cd /usr/lib/dkms
sudo cp dkms_autoinstaller /etc/init.d/
cd /etc/init.d
sudo update-rc.d dkms_autoinstaller defaults

# Probe PSE module, this step is required after every OS reboot, then the device can be opened for communication
sudo modprobe pse
string_pse=$(lsmod | grep pse)

if [[ $string_pse =~ "pse" ]]
then
echo "PSE kernel module installed!"
else
	echo "PSE kernel module installation failed!"
	if [[ $string =~ "Package linux-headers-$(uname -r) is not available" ]]
	then
	echo "Your kernel headers for kernel $(uname -r) cannot be found. It is recommended to install another Linux kernel version."
	fi
fi
