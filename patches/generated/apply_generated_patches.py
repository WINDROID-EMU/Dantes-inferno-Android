#!/usr/bin/env python3

import os
import re
import sys
import glob

def find_file_containing(gen_dir, pattern):
    for filepath in sorted(glob.glob(os.path.join(gen_dir, 'dantes_inferno_recomp.*.cpp'))):
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        if re.search(pattern, content):
            return filepath, content
    return None, None

def apply_patch(filepath, content, check_pattern, find_regex, replacement, description):
    if not filepath:
        print(f"  WARNING: File not found for: {description}")
        return content
    if re.search(check_pattern, content):
        print(f"  Already applied: {description}")
        return content
    match = re.search(find_regex, content, re.DOTALL)
    if match:
        content = re.sub(find_regex, replacement, content, flags=re.DOTALL)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"  Applied: {description} ({os.path.basename(filepath)})")
    else:
        print(f"  WARNING: Pattern not found: {description} ({os.path.basename(filepath)})")
    return content

def main():
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    gen_dir = os.path.join(project_root, 'generated', 'default')

    if not os.path.isdir(gen_dir):
        print(f"ERROR: Generated directory not found: {gen_dir}", file=sys.stderr)
        sys.exit(1)

    filepath, content = find_file_containing(gen_dir, r'DEFINE_REX_FUNC\(sub_82701240\)')
    if filepath:
        content = apply_patch(filepath, content,
            r'FiberSetjmp',
            r'(REX_STORE_U32\(ctx\.r3\.u32 \+ 312, ctx\.r0\.u32\);\n)\s*// li r3,0',
            r'''\g<1>	{
		int fiber_ret = FiberSetjmp(ctx.r3.u32);
		if (fiber_ret != 0) {
			FiberRestoreContext(ctx, base);
			return;
		}
	}
	// li r3,0''',
            "sub_82701240: FiberSetjmp before return")

    filepath, content = find_file_containing(gen_dir, r'DEFINE_REX_FUNC\(sub_8267ACC8\)')
    if filepath:
        content = apply_patch(filepath, content,
            r'g_setjmp_ctx_addr',
            r'(ctx\.lr = 0x8267AD1C;\n)\s*sub_82701240\(ctx, base\);\n(\s*// cmpwi r3,0)',
            r'''\g<1>	g_setjmp_ctx_addr = ctx.r3.u32;
	sub_82701240(ctx, base);
	{
		int fiber_ret = setjmp(g_fiber_jmp_buf);
		if (fiber_ret != 0) {
			FiberRestoreContext(ctx, base);
		}
	}
\g<2>''',
            "sub_8267ACC8: setjmp after sub_82701240")

    filepath, content = find_file_containing(gen_dir, r'DEFINE_REX_FUNC\(sub_82700CE0\)')
    if filepath:
        content = apply_patch(filepath, content,
            r'FiberLongjmp',
            r'(ctx\.r3\.u64 = ctx\.r6\.u64;\n)\s*// blr \n\s*return;\n(loc_82700FCC:)',
            r'''\g<1>	FiberLongjmp(ctx.r6.u32);
	return;
\g<2>''',
            "sub_82700CE0: FiberLongjmp instead of blr")

    filepath, content = find_file_containing(gen_dir, r'DEFINE_REX_FUNC\(sub_82678D78\)')
    if filepath:
        content = apply_patch(filepath, content,
            r'sub_82700CE0\(ctx, base\);\n\s*return;\n\}',
            r'(sub_82700CE0\(ctx, base\);)\n\}',
            r'''\g<1>
	return;
}''',
            "sub_82678D78: return after sub_82700CE0")

    filepath, content = find_file_containing(gen_dir, r'__imp__VdSwap\(ctx, base\);')
    if filepath:
        content = apply_patch(filepath, content,
            r'OnGuestVdSwap',
            r'(\t__imp__VdSwap\(ctx, base\);)',
            r'\g<1>\n\tOnGuestVdSwap();',
            "VdSwap: hook OnGuestVdSwap() for true guest emulation FPS")

    # Fix vpkuwus in-place aliasing in VP6 video decoder (green artifacts fix)
    vpkuwus_pattern = re.compile(
        r'(\t// vpkuwus128 (v\d+),(v\d+),(v\d+)\n)'
        r'(\tctx\.\w+\.u16\[7\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[3\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[6\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[2\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[5\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[1\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[4\] = [^\n]+\n'
        r'\tctx\.\w+\.u16\[0\] = [^\n]+)'
    )
    for fpath in sorted(glob.glob(os.path.join(gen_dir, 'dantes_inferno_recomp.*.cpp'))):
        with open(fpath, 'r', encoding='utf-8') as f:
            fcontent = f.read()
        if vpkuwus_pattern.search(fcontent):
            def repl_vpkuwus(m):
                comment = m.group(1)
                vd = m.group(2)
                va = m.group(3)
                vb = m.group(4)
                return f"{comment}\tsimde_mm_store_si128((simde__m128i*)ctx.{vd}.u16, simde_mm_packus_epi32(simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*)ctx.{vb}.u32), simde_mm_set1_epi32(0xFFFF)), simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*)ctx.{va}.u32), simde_mm_set1_epi32(0xFFFF))));"
            new_fcontent = vpkuwus_pattern.sub(repl_vpkuwus, fcontent)
            if new_fcontent != fcontent:
                with open(fpath, 'w', encoding='utf-8') as f:
                    f.write(new_fcontent)
                print(f"  Applied: vpkuwus in-place aliasing fix ({os.path.basename(fpath)})")

    print("Generated code patches applied.")

if __name__ == '__main__':
    main()
