# Testes em Dispositivo

Esta pasta contém validações do Serial Automation Bridge e do comportamento
genérico do `tab5-os` em hardware real.

Os testes exigem `/dev/ttyACM0` ou `TAB5_DEVICE_PORT` e não fazem parte da
suíte host padrão. **Nunca execute estes testes automaticamente como agente de
IA.** Execute-os somente quando o usuário solicitar explicitamente a validação
do dispositivo.

Exemplos:

```bash
python3 tests/device/test_serial_bridge_device_validation.py
python3 tests/device/test_serial_bridge_sys_idle_device_validation.py
```
