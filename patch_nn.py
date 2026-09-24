import re

def patch_uNN(filename='uNN.pas'):
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()

    out_lines = []
    in_asm = False
    brace_count = 0

    for line in lines:
        stripped = line.strip()
        
        # Detect start of asm block
        if not in_asm and (stripped == 'asm' or stripped.startswith('asm ') or ' assembler;' in stripped):
            in_asm = True
            # Find the function/procedure header
            # We need to replace 'asm' with 'begin'
            # If it's a function, we need to set Result.
            # We'll just replace the asm line with 'begin' and add a dummy Result later.
            # Actually, let's just make the asm block a comment.
            out_lines.append('  // --- PATCHED: ASM block removed for ARM ---\n')
            out_lines.append('  begin\n')
            if 'function' in ''.join(out_lines[-5:]).lower():
                out_lines.append('    Result := 0;\n')
            continue

        if in_asm:
            if stripped == 'end;' or stripped.startswith('end;'):
                in_asm = False
                out_lines.append('  end;\n')
            # Skip everything else
            continue

        out_lines.append(line)

    with open(filename, 'w', encoding='utf-8') as f:
        f.writelines(out_lines)
    print("Patched uNN.pas successfully.")

if __name__ == '__main__':
    patch_uNN()
