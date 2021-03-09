# Kunetik
A sample Linux Kernel Module

## Background
This project is aimed at those that want to learn how to write a linux kernel module (maybe a device driver) but maybe never wrote a one, or maybe they have but they want to learn as much as they can. The source code for the module is full of explanatory comments that describe the use of the functions. For any questions or clarifications, please open a new issue and I (or someone else from the communicaty) will attempt to answer it.  
This kernel module is a sample driver created for a fictional USB device that captures temperature and humidity data.  
The role of the driver is to create a device node (`/dev/kunetik`) that users can interact with to ask the fictional device to collect temperature and humidity data.  
_Note: since we don't really have a USB device, the driver is implemented with the standard `init` and `exit` functions rather than `probe` and `remove`._

## Usage
The module has been tested in Ubuntu 18.04 and 20.04.

### Source Code

#### Via Git
In a terminal, run:  
`$ git clone https://github.com/BryanMorfe/kunetik.git`

#### Download
To download, [click here](https://github.com/BryanMorfe/kunetik/archive/main.zip)

### Compilation & Insertion
Go to the source code directory and execute:  
`$ sudo make install`  
That command will insert the module and set the character device permissions.

### Sample User Program
There is an included program `kunetikc` that uses the `/dev/kunetik/` device to print simulated temperature and humidity data. To use it, simply navigate to the `kunetik_user` sub-directory, compile the program by executing:  
`$ make`  
Finally, run it:  
`$ ./kunetikc`

## Kunetik Device Node
The kunetik device node (`/dev/kunetik`) supports the following file operations:  
- open/close (duh!)
- ioctl:
  - `KTK_SET_TEMP_TYPE` & `KTK_GET_TEMP_TYPE`: Set/Get temperature unit (Celcius or Fahrenheit).  
  - `KTK_CAPTURE_DATA`: Tells the device to "capture" new data and _prepare_ it for a user read.  
- read: reads latest captured data when available (otherwise it blocks or return `-EAGAIN` until new data is available).  

For an example on how this device node is used, check the provided user program (`kunetikc`).
