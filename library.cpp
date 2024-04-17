void printBufferHex(const char* buffer, int size, const char* label) {
    printf("%s:\n", label);
    for (int i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]);
        if ((i + 1) % 16 == 0) // Print 16 bytes per line for better readability.
            printf("\n");
    }
    printf("\n");
}



        printBufferHex(buffer, SECTOR_SIZE * size, "Buffer After Read");
        printBufferHex(reference_buffer, SECTOR_SIZE * size, "Reference Buffer");