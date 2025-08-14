# DeskTape_extractor
DeskTape was a tool on MacOs that would mount a tape as a drive, so the user could easily drop files to backup to the tape. Nowadays, there isn't any easy way to extract the content of a backup tape created by DeskTape. This tool allow you to do that by converting a tape image into a mountable HFS disk.

## Building
Currently only builds on Windows with visual studio using the provided .sln solution. But it should be trivial to build on other platforms.

## Usage
You can either convert a single tape image, or all images in a folder:
```
tapeExtract.exe pathToTape\tape.bin
```
```
tapeExtract.exe pathToTapes\*.bin
```

An output folder will be created with a subfolder for each tape image. This is where the HFS images will be created (in addition to a variety of logs).

## Limitations
Currently only support raw files or .cptp files generated from DiscImageChef.  
Also **only the first session** of the tape will be extract at the current time. Additional session support is being worked on.
Only tapes created by DeskTape 1.5 and 2.0 have been tested so far. If you have backups from other versions, feel free to open an issue.
