import gzip
import json
from pathlib import Path

libpath = Path(__file__).resolve().parent / "ayushsharma82-ElegantOTA"
source = libpath / "CurrentPlainHTML.txt"
gzipped = libpath / "CurrentPlainHTML.txt.gz"
header = libpath / "src/elop.h"
cpp = libpath / "src/elop.cpp"

plain = source.read_bytes()
gzipbytes = gzip.compress(plain, 9, mtime=0)
origin = "gzip -9"

if gzipped.exists():
    override = gzipped.read_bytes()
    try:
        matches = gzip.decompress(override) == plain
    except OSError:
        matches = False
    if matches:
        gzipbytes, origin = override, gzipped.name
    else:
        print(f"IGNORED {gzipped.name}: does not decode to {source.name}")

intlist = [int(one) for one in gzipbytes]
content = json.dumps(intlist).replace("[", "{").replace("]", "}").replace(" ", "")
headertext = header.read_text()
header.write_text(headertext[:1+headertext.find("[")]+str(len(gzipbytes))+headertext[headertext.find("]"):])
cpptext = cpp.read_text()
first_bracket = cpptext.find("[")
second_bracket = cpptext.find("]")
corrected_bytes = cpptext[:1+first_bracket]+str(len(gzipbytes))+cpptext[second_bracket:]
cppout = corrected_bytes[:corrected_bytes.find("{")]+content+corrected_bytes[corrected_bytes.find(";"):]+"\n"
cpp.write_text(cppout)
print("File content updated from", source.resolve(), f"({origin})")
print("Bytes fixed:", cpptext[1+first_bracket:second_bracket], "to", len(gzipbytes))
