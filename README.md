# Laboratorio 2 — Catálogo musical indexado y confiable

Implementación del Laboratorio 2 de Estructura de Datos II: lectura segura de registros binarios por offset, índice primario ordenado, índice secundario invertido por compositor y verificación de consistencia entre el índice y el archivo de datos.

## Estudiante
Nombre: Luis Fernando Noriega Mejia

## Complejidad de las operaciones principales

- **Construir el índice primario** (`build_primary_index`): O(n log n). Recorrer el archivo es O(n), pero ordenar las entradas por `label_id` es O(n log n) y domina el costo total.
- **Búsqueda primaria** (`find_offset`): O(log n). Búsqueda binaria manual sobre el arreglo ya ordenado.
- **Construir el índice secundario** (`build_composer_index`): O(m log m), donde m es el número de entradas primarias. Se ordenan los pares (compositor, label_id) una sola vez.
- **Búsqueda secundaria** (`find_by_composer`): O(log k), donde k es el número de compositores distintos. También es búsqueda binaria manual.
- **Verificar consistencia** (`verify_primary_index`): O(n) en promedio. Recorre el índice una vez usando `unordered_set` para detectar claves y offsets duplicados, y hace una lectura por cada entrada.

## Diferencia entre integridad física y consistencia lógica

### Integridad física

Pregunta si los bytes leídos son los mismos que se escribieron. Ejemplos: header incompleto, payload truncado, CRC que no coincide con el payload.

### Consistencia lógica

Pregunta si las estructuras y referencias tienen sentido para el sistema, aunque los bytes estén físicamente intactos. Ejemplos: magic o versión incorrectos, longitud fuera de rango, payload mal formado, clave del índice distinta a la del registro, claves primarias duplicadas, índice desordenado, dos claves apuntando al mismo offset.

Un registro puede tener CRC válido y aun así estar asociado a la clave equivocada: el CRC solo prueba integridad física, no consistencia lógica.

## Descripción de las pruebas adicionales en `tests/student_tests.cpp`

- **ST1**: `build_primary_index` con un archivo vacío. Verifica que devuelva `BuildStatus::Ok` con un índice vacío.
- **ST2**: `read_record_at` con un `payload_length` inválido (el máximo valor posible de `uint32_t`) y sin ningún byte de payload después del header. Verifica que devuelva `InvalidLength` sin intentar leer ni reservar memoria para ese payload.
- **ST3**: `verify_primary_index` con un índice desordenado que además tiene una clave duplicada en posiciones no adyacentes. Verifica que el reporte contenga tanto `UnsortedIndex` como `DuplicateKey`.

## Compilar

```bash
cmake -S . -B build
cmake --build build
```

## Ejecutar pruebas

```bash
ctest --test-dir build --output-on-failure
```

O directamente:

```bash
./build/catalog_tests
```

Para ejecutar un ejercicio específico:

```bash
./build/catalog_tests --test-case="E04*"
```

Los 6 TODO obligatorios están completos. Los 10 tests provistos por el profesor (`E01`–`E10`) y las 3 pruebas propias (`ST1`–`ST3`) pasan. El bono opcional `intersect_sorted` no fue implementado.

## Generar datos de ejemplo

```bash
./build/catalog_generate data/catalog.psv data/catalog.bin
```

## Usar la aplicación

```bash
./build/lab2_catalog build data/catalog.bin data/catalog.idx
./build/lab2_catalog find data/catalog.bin data/catalog.idx DG18807
./build/lab2_catalog composer data/catalog.bin data/catalog.idx BEETHOVEN
./build/lab2_catalog verify data/catalog.bin data/catalog.idx
```

## Simular corrupción

```bash
./build/catalog_corrupt flip data/catalog.bin data/catalog.corrupt 20
./build/lab2_catalog verify data/catalog.corrupt data/catalog.idx

./build/catalog_corrupt truncate data/catalog.bin data/catalog.truncated 3
./build/lab2_catalog verify data/catalog.truncated data/catalog.idx
```

## Archivos que ya están completos

- `src/binary_io.cpp`: I/O little-endian y utilidades de stream.
- `src/crc32.cpp`: CRC-32/ISO-HDLC.
- `src/catalog_codec.cpp`: codificación y decodificación del payload.
- `src/index_io.cpp`: persistencia del índice primario.
- `src/main.cpp`: interfaz de línea de comandos.
- `tools/`: generación y corrupción controlada de datos.

