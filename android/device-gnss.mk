PRODUCT_PACKAGES += android.hardware.gnss@1.1 \
        android.hardware.gnss@1.1-impl \
        android.hardware.gnss@1.1-service.gpsd \
        gpsd
PRODUCT_PROPERTY_OVERRIDES += \
        service.gpsd.parameters=-Nn,-G,/dev/ttyACM0

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml
