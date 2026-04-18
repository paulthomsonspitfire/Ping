-- AppleScript-only serial generator. No Python. Uses keygen binary.
-- Rebuild keygen for your Mac first: ./build_keygen.sh
set toolsDir to (POSIX path of (path to me)) as text
set toolsDir to do shell script "dirname " & quoted form of toolsDir

set nameResp to display dialog "Customer name:" default answer "" with title "P!NG Serial Generator"
set custName to text returned of nameResp
if custName is "" then return

set tierResp to display dialog "Tier (demo, standard, pro):" default answer "pro" with title "P!NG Serial Generator"
set tier to text returned of tierResp
if tier is "" then set tier to "pro"

set expiryResp to display dialog "Expiry (YYYY-MM-DD):" default answer "2027-12-31" with title "P!NG Serial Generator"
set expiry to text returned of expiryResp
if expiry is "" then set expiry to "2027-12-31"

set serialResult to do shell script "cd " & quoted form of toolsDir & " && ./keygen --name " & quoted form of custName & " --tier " & quoted form of tier & " --expiry " & quoted form of expiry
display dialog serialResult with title "Serial" buttons {"OK"} default button 1
