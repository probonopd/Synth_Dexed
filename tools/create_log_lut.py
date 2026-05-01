import math

TABLE_SIZE = 1024

def generate_linear_to_db_table():
    linear_to_db_table = []
    for i in range(TABLE_SIZE):
        linear_value = i / (TABLE_SIZE - 1)
        db_value = 20.0 * math.log10(linear_value + 1e-9)  # +1e-9 um log(0) zu vermeiden
        fixed_db_value = int(db_value * (1 << 16))
        linear_to_db_table.append(fixed_db_value)
    return linear_to_db_table

def generate_db_to_linear_table():
    db_to_linear_table = []
    for i in range(TABLE_SIZE):
        db_value = (i / (TABLE_SIZE - 1)) * 2.0  # Wertebereich von 0 bis 2 dB
        linear_value = 10.0 ** (db_value / 20.0)
        fixed_linear_value = int(linear_value * (1 << 16))
        db_to_linear_table.append(fixed_linear_value)
    return db_to_linear_table

def write_header_file(filename, linear_to_db_table, db_to_linear_table):
    with open(filename, 'w') as f:
        f.write("#ifndef LOOKUP_TABLES_H\n")
        f.write("#define LOOKUP_TABLES_H\n\n")
        f.write("#define TABLE_SIZE {}\n\n".format(TABLE_SIZE))
        
        f.write("const int32_t linear_to_db_table[TABLE_SIZE] = {\n")
        c=0
        f.write("    ")
        for value in linear_to_db_table:
            f.write("{}, ".format(value))
            c=c+1
            if(c==8):
                f.write("\n    ")
                c=0
        f.write("};\n\n")
        
        c=0
        f.write("    ")
        f.write("const int32_t db_to_linear_table[TABLE_SIZE] = {\n")
        for value in db_to_linear_table:
            f.write("{}, ".format(value))
            c=c+1
            if(c==8):
                f.write("\n    ")
                c=0
        f.write("};\n\n")
        
        f.write("#endif // LOOKUP_TABLES_H\n")

linear_to_db_table = generate_linear_to_db_table()
db_to_linear_table = generate_db_to_linear_table()
write_header_file("lookup_tables.h", linear_to_db_table, db_to_linear_table)
