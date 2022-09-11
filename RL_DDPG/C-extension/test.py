#!/usr/bin/env python3

import shmextension

print(shmextension.hello());
print(shmextension.write_shm(5, "StarNight"));
#help(shmextension);
