obj-m += beadapanel.o
beadapanel-y := bp_drv.o bp_model.o bp_bl.o bp_connector.o bp_pipe.o bp_sysfs.o bp_fbdev.o
ifeq ($(MAKING_MODULES),1)
-include $(TOPDIR)/Rules.make
endif
