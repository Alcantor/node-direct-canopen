const co = require('./direct-canopen.js');
let node = co.create_node("can0", 1);

function hanlde_sdo_upload_result(data){
	if(data instanceof Error){
		console.log(data);
		return;
	}
	console.log("Number outputs: "+ new Int8Array(data)[0]+" data length "+data.byteLength);
}

/* Configure the RX PDO 0 of the Wago Wago 750-338 / 750-337 */
co.send_sdo_uint32(node, 0x1400, 1, 0x80000000); /* Invalid COB - disable PDO 0 */
co.send_sdo_uint8(node, 0x1400, 2, 255); /* Transfer type */
co.send_sdo_uint8(node, 0x1600, 0, 0); /* Number of mapped object for PDO 0 */
co.send_sdo_uint32(node, 0x1600, 1, 0x62000008+0x100*1); /* Mapped object */
co.send_sdo_uint8(node, 0x1600, 0, 1); /* Number of mapped object for PDO 0 */
co.send_sdo_uint8(node, 0x6206, 1, 0xFF); /* Error Mode Digital Output 8-Bit 1st Block */
co.send_sdo_uint8(node, 0x6207, 1, 0xFF); /* Error Value Digital Output 8-Bit 1st Block */
co.send_sdo_uint32(node, 0x1400, 1, 0x200+1); /* Valid COB - enable PDO 0 */

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

