# vproc
a graphical version of ps for 9front / plan 9


![vproc](vproc.png)


## Keyboard shortcuts
q / Del to quit

Up / Down to scroll one line

Page Up / Down to scroll 10 lines

## Usage
By giving vproc the **-h flag** it will spit out the following:

``vproc [-a] [-i] [h] [-d] <seconds>``

The **-a flag** displays the arguments passed to each process.

The **-i flag** reverses the sorting (normally it is sorted by the pid like ps)

The **-h flag** displays the help message

The **-r flag** displays the elapsed real time

The **-d flag** sets the delay (in seconds) between each fetch cycle.\
Note: The default value is once every 5 seconds.

## Bugs
Yes.

Could it have been written in a better way?

Yes, definitely. This is my first time writing Plan9 C.\
Granted, vproc is mostly based off the ps source code and other projects.\
But ways to improve are always welcome so pull requests are very much appreciated.

## Features missing
You tell me. Here's my current TODO in no particular order:
- [ ] implement a way to be able to select and kill a process from the list
- [ ] man page
- [ ] use bio.h functions and hopefully up the performance a bit
- [ ] add functionality to sort by the different columns
