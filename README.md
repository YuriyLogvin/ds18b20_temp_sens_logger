# Ds18b20 temperature sensors loger

Source based on "Simple HTTP File Server Example"(file_serving) from ESP32 examples
and parts from esp-idf-lib library

Can scanning OneWire ds18b20 sensors. And show scan results on the web page.
On the push button "Start logging" - it creates a new file and every 2 seconds write scan sensors results.

## Note


## Usage

* Open the project configuration menu (`idf.py menuconfig`) go to `Example Configuration` ->
    1. WIFI SSID: WIFI network to which your PC is also connected to.
    2. WIFI Password: WIFI password
    3. WiFi Connection type: check/uncheck Access point mode

* Attach Ds18b20 to pin IO21 on esp32 board

* In order to test the file server demo :
    1. compile and burn the firmware `idf.py -p PORT flash`
    2. run `idf.py -p PORT monitor` and note down the IP assigned to your ESP module. The default port is 80
    3. test the example interactively on a web browser (assuming IP is 192.168.43.130):
        1. open path `http://192.168.43.130/` or `http://192.168.43.130/index.html` to see an HTML web page with list of files on the server (initially empty)
        2. use the file upload form on the webpage to select and upload a file to the server
        3. click a file link to download / open the file on browser (if supported)
        4. click the delete link visible next to each file entry to delete them
    4. test the example using curl (assuming IP is 192.168.43.130):
        1. `myfile.html` is uploaded to `/path/on/device/myfile_copy.html` using `curl -X POST --data-binary @myfile.html 192.168.43.130:80/upload/path/on/device/myfile_copy.html`
        2. download the uploaded copy back : `curl 192.168.43.130:80/path/on/device/myfile_copy.html > myfile_copy.html`
        3. compare the copy with the original using `cmp myfile.html myfile_copy.html`

* To write to SD card, you need to:
    1. Select the `Mount the SD card to the filesystem` in the configuration menu (by calling `idf.py menuconfig` and select the `EXAMPLE_MOUNT_SD_CARD` option.
    2. If you need to format the card while the card fails to be mounted, enable the config option `The card will be formatted if mount has failed` (`EXAMPLE_FORMAT_SDCARD_IF_MOUNT_FAILED`). Be careful, all the data in the card will disappear.

    Note: You will have to access the SD card by SPI bus with sdspi driver, if you are using ESP32S2.

## Note

Browsers often send large header fields when an HTML form is submit. Therefore, for the purpose of this example, `HTTPD_MAX_REQ_HDR_LEN` has been increased to 1024 in `sdkconfig.defaults`. User can adjust this value as per their requirement, keeping in mind the memory constraint of the hardware in use.
