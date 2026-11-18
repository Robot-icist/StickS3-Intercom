Import("env")

env.AddCustomTarget(
    name="m5burner_push",
    dependencies=["buildfs"],
    actions=[
        '"$PYTHONEXE" "scripts/push_m5burner_bins.py"'
    ],
    title="Push M5Burner bins",
    description="Build filesystem image then copy bootloader/partitions/firmware/LittleFS into firmware_* folders",
)