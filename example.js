const co = require('./direct-canopen.js');
let node = co.create_node("can0", 1);

/*
// Test with "node --expose-gc example.js"
function finalize_test(){
	let node2 = co.create_node("can0", 2);
	console.log("Garbage collector");
}
finalize_test();
global.gc();
*/
function hanlde_sdo_upload_result(data){
	if(data instanceof Error){
		console.log(data);
		return;
	}
	console.log("Number outputs: "+ new Int8Array(data)[0]+" data length "+data.byteLength);
}

function result_cb(resp){
	if(resp instanceof Error){
		console.log(resp);
		return;
	}
	console.log("OK");
}
node.sdo_upload_uint8(0x6200, 0, u8 => { console.log("Number outputs: "+u8); });

/* Configure the RX PDO 0 of the Wago Wago 750-338 / 750-337 */
node.sdo_download_uint32(0x1400, 1, 0x80000000, result_cb); /* Invalid COB - disable PDO 0 */
node.sdo_download_uint8(0x1400, 2, 255, result_cb); /* Transfer type */
node.sdo_download_uint8(0x1600, 0, 0, result_cb); /* Number of mapped object for PDO 0 */
node.sdo_download_uint32(0x1600, 1, 0x62000008+0x100*1, result_cb); /* Mapped object */
node.sdo_download_uint8(0x1600, 0, 1, result_cb); /* Number of mapped object for PDO 0 */
node.sdo_download_uint8(0x6206, 1, 0xFF, result_cb); /* Error Mode Digital Output 8-Bit 1st Block */
node.sdo_download_uint8(0x6207, 1, 0xFF, result_cb); /* Error Value Digital Output 8-Bit 1st Block */
node.sdo_download_uint32(0x1400, 1, 0x200+1, result_cb); /* Valid COB - enable PDO 0 */

/* Go into operational mode */
setTimeout(function() {
	node.nmt_send(co.NMT_OPERATIONAL);
}, 500);

/* Test to switch the digital output */
t = new ArrayBuffer(1);
v = new Uint8Array(t);
i = 0;
setTimeout(function() {
	setInterval(function() {
		v[0] = 1 << (i++%8);
		node.pdo_send(0, t);
		console.log("Done");
	}, 100);
}, 2000);

node.pdo_recv(function(pdoid, data){
	v2 = new Uint8Array(data);
	console.log("RECV: "+pdoid+" "+v2[0]);
});

