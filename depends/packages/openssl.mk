package=openssl
$(package)_version=3.5.9
$(package)_download_path=https://github.com/openssl/openssl/releases/download/openssl-$($(package)_version)
$(package)_file_name=openssl-$($(package)_version).tar.gz
$(package)_sha256_hash=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a

# Qt's Linux/FreeBSD TLS backend only. Headless depends builds omit Qt and
# therefore omit this package; Windows/macOS use their native trust backends.
define $(package)_set_vars
  $(package)_config_opts=no-shared no-tests no-module no-dso no-engine no-comp no-zlib no-asm
  $(package)_config_opts+=--prefix=$(host_prefix) --libdir=lib
  $(package)_config_env=CC="$$($(package)_cc)" AR="$$($(package)_ar)" RANLIB="$$($(package)_ranlib)"
  $(package)_config_opts_linux=linux-generic64
  $(package)_config_opts_freebsd=BSD-generic64
  ifneq ($(filter i686 arm armv7l,$(host_arch)),)
    $(package)_config_opts_linux=linux-generic32
    $(package)_config_opts_freebsd=BSD-generic32
  endif
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts) $(filter-out -std=%,$($(package)_cflags)) $($(package)_cppflags)
endef

define $(package)_build_cmds
  $(MAKE) build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_dev
endef
