#!/usr/bin/env bash
# Post-tool-use hook: rebuild after edits to C/H files and feed errors back.
python3 -c "
import json, re, subprocess, sys, os

BUILD_DIR = '/home/ahmet/personal_projects/atc_hdlc/build'

try:
    data = json.load(sys.stdin)
    tool_input = data.get('tool_input', {})
    file_path  = tool_input.get('file_path', '')

    if not file_path.endswith(('.c', '.h')):
        sys.exit(0)

    result = subprocess.run(
        ['make', '-j4'],
        cwd=BUILD_DIR,
        capture_output=True,
        text=True,
        timeout=60
    )

    output = result.stderr + result.stdout

    # Collect errors and warnings, skip generic 'make[N]' noise
    lines = [l for l in output.splitlines()
             if re.search(r'error:|warning:|undefined', l, re.IGNORECASE)
             and not l.strip().startswith('make')]

    if result.returncode != 0 or lines:
        snippet = '\n'.join(lines[:30])
        if len(lines) > 30:
            snippet += f'\n... ({len(lines)-30} more lines)'

        status = 'BUILD FAILED' if result.returncode != 0 else 'BUILD WARNINGS'
        msg = f'{status} after editing {os.path.basename(file_path)}:\n\n{snippet}'

        print(json.dumps({
            'hookSpecificOutput': {
                'hookEventName': 'PostToolUse',
                'additionalContext': msg
            }
        }))
except subprocess.TimeoutExpired:
    print(json.dumps({
        'hookSpecificOutput': {
            'hookEventName': 'PostToolUse',
            'additionalContext': 'Build timed out after 60s.'
        }
    }))
except Exception:
    sys.exit(0)
"
