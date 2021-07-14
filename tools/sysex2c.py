#!/usr/bin/python3

import sys

# From: https://github.com/bwhitman/learnfm/blob/master/dx7db.py
def unpack_packed_patch(p):
    # Input is a 128 byte thing from compact.bin
    # Output is a 156 byte thing that the synth knows about
    o = [0]*156
    for op in range(6):
        o[op*21:op*21 + 11] = p[op*17:op*17+11]
        leftrightcurves = p[op*17+11]
        o[op * 21 + 11] = leftrightcurves & 3
        o[op * 21 + 12] = (leftrightcurves >> 2) & 3
        detune_rs = p[op * 17 + 12]
        o[op * 21 + 13] = detune_rs & 7
        o[op * 21 + 20] = detune_rs >> 3
        kvs_ams = p[op * 17 + 13]
        o[op * 21 + 14] = kvs_ams & 3
        o[op * 21 + 15] = kvs_ams >> 2
        o[op * 21 + 16] = p[op * 17 + 14]
        fcoarse_mode = p[op * 17 + 15]
        o[op * 21 + 17] = fcoarse_mode & 1
        o[op * 21 + 18] = fcoarse_mode >> 1
        o[op * 21 + 19] = p[op * 17 + 16]
    
    o[126:126+9] = p[102:102+9]
    oks_fb = p[111]
    o[135] = oks_fb & 7
    o[136] = oks_fb >> 3
    o[137:137+4] = p[112:112+4]
    lpms_lfw_lks = p[116]
    o[141] = lpms_lfw_lks & 1
    o[142] = (lpms_lfw_lks >> 1) & 7
    o[143] = lpms_lfw_lks >> 4
    o[144:144+11] = p[117:117+11]
    o[155] = 0x3f

    # Clamp the unpacked patches to a known max. 
    maxes =  [
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc6
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc5
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc4
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc3
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc2
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc1
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, # pitch eg rate & level 
        31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48, # algorithm etc
        126, 126, 126, 126, 126, 126, 126, 126, 126, 126, # name
        127 # operator on/off
    ]
    for i in range(156):
        if(o[i] > maxes[i]): o[i] = maxes[i]
        if(o[i] < 0): o[i] = 0
    return o

def print_header_data(decoded_voice):
	print("\t\t\t",end="")
	for y in range(0,156):
		if((y+1)%8==0):
			print("%02d," % decoded_voice[y],end="")
			print("\n\t\t\t",end="")
		else:
			if(y!=155):
				print("%02d," % decoded_voice[y],end="")
			else:
				print("%02d" % decoded_voice[y])

#---------------------------------------------------------------------------

print("uint8_t bank[%d][32][156] =\n{" % int(len(sys.argv)-1))
for sysex in sys.argv[1:]:
	print("\t{")
	with open(sysex, "rb") as f:
		header = f.read(6)
		if(header[0]!=240):
			print("* Start of sysex not found.")
			exit(200)
		if(header[1]!=67):
                	print("* Manufactorer-ID not Yamaha.")
                	exit(201)
		if(header[3]!=9):
                	print("* Not a 32 voice sysex file.")
                	exit(202)
		byte_count = header[4]*128+header[5]
		if(byte_count!=4096):
                	print("* Byte count mismatch.")
                	exit(203)
		patch_data=f.read(4096)
		check = ~sum(patch_data) + 1 & 0x7F
		f.seek(4102) # Bulk checksum
		checksum=int.from_bytes(f.read(1),"little")
		if(check!=checksum):
			print("* Checksum mismatch!")
			exit(204)
		f.seek(6)

		for v in range(1,33):
			data=f.read(128)
			print("\t\t{")
			print_header_data(unpack_packed_patch(data))
			if(v!=32):
				print("\t\t},", end="")
			else:
				print("\t\t}", end="")

			print(" // %d: %s" % (v, data[118:128].decode('ascii')))
		if(sys.argv[len(sys.argv)-1]==sysex):
			print("\t}",end="")
		else:
			print("\t},",end="")
		print(" // %s" % sysex)
print("};")
