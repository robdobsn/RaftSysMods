# ToDo

[] check why BLE doesn't disconnect immediately when terminated from webble
[] try changing the LL_PACKET_TIME param
[] try other MAX_BLE_PACKET_LEN_DEFAULT values
[] use GAP TX indication to manage buffers
[] implement an incremental encode - or encode buffer as much as possible - to maximise MTU
[] BLE_MIN_TIME_BETWEEN_OUTBOUND_MSGS_MS ??
[] could possibly have a rate-rec in pubList in config which specifies the name of a websocket and when a websocket connects which has a matching base-name (e.g. if interface is specified as devices and new websocket is devices_0) then it automatically adds a record to the publist maintained by the state publisher so that publishing is done on this interface without the need for a subscription message from the websocket?
 