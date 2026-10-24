"""Package the complete resource-only POC using an explicit metadata/source allowlist."""
from pathlib import Path


def main():
    # Reuse the reviewed metadata-only packager without changing historical output.
    template = Path(__file__).with_name('package_full_poc.py').read_text(encoding='utf-8')
    template = template.replace('NXEmu-Shader-BKTR-Full-POC-20261023',
                                'NXEmu-Shader-Complete-POC-20261023')
    template = template.replace('FULL_BKTR_POC.md', 'COMPLETE_RESOURCE_POC.md')
    template = template.replace('MarioKart-full-555-logical.nxshader',
                                'MarioKart-resource-spans.nxshader')
    template = template.replace('from full_recipe_manifest import WORK',
        "from rebuild_recipes import REPO\nWORK=REPO/'build/shader-origin-poc'")
    start = template.index("for name in ['verified-results.json'")
    end = template.index("sources=", start)
    template = template[:start] + '''for name in ['verified-results.json','tail-audit.json',
             'trimmed-discovery.json','remaining-discovery.json']:
    files[name]=WORK/name
files['final-comparison.json']=REPO/'build/shader-bktr-poc/complete-resource-spans-comparison.json'
''' + template[end:]
    template = template.replace("sources=['nxshader.py'", "sources=['shader_span.py','test_shader_span.py',"
        "'audit_shader_tails.py','resolve_trimmed_shaders.py','scan_remaining_spans.py',"
        "'verify_complete_spans.py','package_complete_poc.py','package_full_poc.py','nxshader.py'")
    exec(compile(template, str(Path(__file__)), 'exec'), {'__name__': '__main__'})


if __name__ == '__main__':
    main()
