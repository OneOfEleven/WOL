# WOL

<div align="center">
<img src="/image1.png">
</div>

# About

Wake-on-Lan windows utility.

This little utility will send WOL packets to a desired unit on your LAN to wake it up.
It will then also ping the unit (for up to 2 mins) until it gets a reply.

We use it to turn-on/power-up our ZyXEL NAS drive which is configured to automatically
turn-off/power-down in the early hours of every day.

The NAS drive has two MAC addresses we can send too, the main one, plus an undocumented
one (00:00:00:00:00:30) that can be used after a power cut - hence the option of specifiying
two different MAC addresses.

Seems on some Windows-10 installs this util needs to be run with admin access to allow
sending ping packets.

### Companion Tools

* [Borland C++Builder software] Borland C++ Builder v6 to compile and create the .exe

### Contributors

* [@OneOfEleven](https://github.com/OneOfEleven/)
