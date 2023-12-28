# vproc
a graphical version of ps(1) for 9front

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

## Features missing
Yes.
