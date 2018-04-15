#!/usr/bin/env python
def patch_meson_compile_commands(outdir):
  from os.path import join
  from re import sub
  
  compdb = join(outdir, "compile_commands.json")

  # Remove stuff that clangd doesn't like.
  cmd_strip = ["ccache\s",
               "-pipe",
               "-MMD",
               "-o\s'.*\.o'",
               "-MF\s'.*\.o.d'",
               "-MQ\s'.*\.o'"]

  with open(compdb, 'r') as f:
    lines = f.readlines()

  with open(compdb, 'w') as f:
      for line in lines:
        for r in cmd_strip:
          line = sub(r, '', line)
        f.write(line)

if __name__ == '__main__':
  import sys
  assert len(sys.argv) >= 2
  patch_meson_compile_commands(sys.argv[1])