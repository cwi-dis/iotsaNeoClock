"""Encode textual alert files into binary alert files"""
import sys
import struct

def binEncode(input, output):
    for line in input:
        items = line.split()
        items = map(int, items)
        duration = items[0]
        del items[0]
        assert len(items) % 3 == 0
        count = len(items) / 3
        format = ">ll" + (count*"xBBB")
        data = struct.pack(format, duration, count, *items)
        output.write(data)
        
def main():
    if len(sys.argv) <= 1:
        binEncode(sys.stdin, sys.stdout)
    elif len(sys.argv) == 2:
        binEncode(open(sys.argv[1], sys.stdout))
    elif len(sys.argv) == 3:
        binEncode(open(sys.argv[1]), open(sys.argv[2], "wb"))
    else:
        print >>sys.stderr, "Usage: %s [asciiAlertFile [binaryAlertFile]]" % sys.argv[0]
        sys.exit(1)
        
if __name__ == '__main__':
    main()
    
