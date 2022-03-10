# Тестовое для компании Syntacore
[задание](https://syntacore.com/media/files/trial_task_sw.pdf)

Интерфейсы:
- read
- ioctl
- proc

## Внутреннее устройство:
При загрузке модуля и установке режима сохраняется время системы и значение jiffies. 

Если запрашивается время в реальном режиме, возвращается реальное время. 

В ускоренном и замедленном режиме читается значение jiffies, считается разница с запомненным значением, умножается на коэффициент и прибавляется к запомненому времени.

В случайном режиме читается значение jiffies, к нему прибавляется случайная величина, а потом, как в ускоренно режиме, разница прибавляется к запомненному времени.
