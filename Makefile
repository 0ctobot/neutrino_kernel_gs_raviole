ifeq ($(CONFIG_SAMSUNG_PRODUCT_SHIP), y)
  obj-$(CONFIG_TOUCHSCREEN_SEC_TS)   += sec_ts.o sec_ts_fw.o sec_ts_fn.o sec_cmd.o
else
  obj-$(CONFIG_TOUCHSCREEN_SEC_TS)   += sec_ts.o sec_ts_fw.o sec_ts_fn.o sec_cmd.o sec_ts_only_vendor.o
endif

KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_SEC_TS=m

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)
