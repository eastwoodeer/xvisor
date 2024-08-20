import binascii
import re
import sys

def gen_gcda(input_string):
    match = re.match(r"^\*(.+)<([0-9a-f]+)", input_string)
    if match:
        filename = match.group(1)
        hex_data = match.group(2)

        binary_data = binascii.unhexlify(hex_data)

        with open(filename, "wb") as file:
            file.write(binary_data)

        print(f"save gcda to: {filename}")


if __name__ == '__main__':
    with open(sys.argv[1], 'r') as fd:
        for line in fd.readlines():
            gen_gcda(line)