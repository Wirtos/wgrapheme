```Cw
char str[] = "іу👨‍👨‍👧‍👧віу";

wgrapheme_status_t status;
size_t len = strlen(str);
size_t offset = 0;
size_t next_offset;

while ((status = wgrapheme_next_boundary(str, len, offset, &next_offset)) == WGRAPHEME_OK)  {
    printf("%.*s\n", (int)(next_offset - offset), &str[offset]);
    offset = next_offset;
}

if (status != WGRAPHEME_DONE) {
    puts("Error while decoding string");
}
```
```text
і
у
👨‍👨‍👧‍👧
в
і
у
```
