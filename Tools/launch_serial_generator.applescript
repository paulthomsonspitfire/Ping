-- Launches the Serial Generator Python GUI. Script is in Resources. Looks for private_key.bin in Tools (parent of this app).
set bundlePosix to POSIX path of (path to me)
set resourcesDir to bundlePosix & "/Contents/Resources"
set scriptPath to resourcesDir & "/serial_generator.py"
do shell script "cd " & quoted form of resourcesDir & " && /usr/bin/python3 " & quoted form of scriptPath
