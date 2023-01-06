# vproc
a graphical version of ps for 9front / plan 9


![vproc](vproc.png)


## Keyboard shortcuts
q / Del to quit

Up / Down to scroll one line

Page Up / Down to scroll 10 lines

## Usage
By giving vproc the -h flag it will spit out the following:

``vproc [-d seconds] [-h] [-i]``

The **-d flag** sets the delay (in seconds) between each fetch cycle.
The default value is once every 5 seconds.

The **-i flag** reverses the sorting (normally it is sorted by the pid like ps)

The **-h flag** displays the help message

## Bugs
Yes.

Could it have been written in a better way?

Yes, definitely. This is my first time writing Plan9 C.\
Granted, vproc is mostly based off the ps source code.\
But ways to improve are always welcome so pull requests are very much appreciated.

## Features missing
Many. In the future I would like..
* to be able to kill a process from the list as well
* a man page
* grabbing the scroll bar and moving it freely with the mouse
* display real time
* display arguments of each process

