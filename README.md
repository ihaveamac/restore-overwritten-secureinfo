# restore-overwritten-secureinfo

Copies serial number from inspect.log from TWLNAND to SecureInfo_A/B.

If you don't have a reason to use it, don't!

## Process

* Checks SecureInfo_A/B to see if a restore is needed at all. This prevents random users from running this on their consoles that don't have this specific issue.
* Moves original SecureInfo_A/B and SecureInfo_C to a backup name.
* Copies Lazarus3DS SecureInfo file to SecureInfo_A/B, using current firmware region.
* Creates new file at SecureInfo_C with console serial number taken from twln/sys/log/inspect.log, writes the region bit, and a message in the signature area with a link to this repo and the current date and time.
