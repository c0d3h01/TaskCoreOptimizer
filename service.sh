(
  until [ "$(getprop sys.boot_completed)" -eq 1 ]; do
    sleep 1
  done

  sleep 30                               #sleep_rest_after_boot.
  /system/bin/task_optimizer 2>/dev/null #run_main_script.
)
