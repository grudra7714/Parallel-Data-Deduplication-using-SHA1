# vim:syn=python
# encoding: ISO8859-1

APPNAME = 'divvy'
VERSION = '0.9'

def options(ctx):
  ctx.load('compiler_c')

def configure(conf):
  conf.load('compiler_c')
  conf.find_program('mpicc')
  conf.check_cc(function_name='ceil', header_name='math.h', lib='m', uselib_store='M')
  conf.check_cc(lib='crypto', uselib_store='OPENSSL', mandatory=True)
  conf.check(uselib='OPENSSL', function_name='SHA1', header_name='openssl/sha.h', mandatory=True)
  conf.check_cfg(path='mpicc', package='', uselib_store='mpi', args='-show')
  conf.check_cfg(package='libpcre', args='--cflags --libs', uselib_store='pcre')

def build(bld):
  bld.program(source=['parallelDeduplication.c','timing.c'], target='parallelDeduplication', use='mpi pcre M OPENSSL MYSQL')
  bld.program(source=['concat.c','timing.c'], target='concat', use='mpi M OPENSSL')

