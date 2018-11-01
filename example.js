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

/* Configure the TX PDO 0 for max 64 digital inputs */
function wago_pdo0tx_input(con, nb_bl_in){
	var promises = [
		/* Invalid COB - disable PDO 0 */
		con.sdo_download_uint32(0x1800, 1, 0x80000000),
		/* Transfer type */
		con.sdo_download_uint8 (0x1800, 2, 255),
		/* Inhibit Time (x100us) */
		con.sdo_download_uint16(0x1800, 3, 100),
		/* Event Timer (x1ms) */
		con.sdo_download_uint16(0x1800, 5,   0),
		/* Global intterrupt enable digital */
		con.sdo_download_uint8 (0x6005, 0,   1),
		/* Number of mapped object for PDO 0 */
		con.sdo_download_uint8 (0x1A00, 0,   0)
	]
	for(var i=1; i<=nb_bl_in; ++i) promises.push(
		/* Mapped object */
		con.sdo_download_uint32(0x1A00, i, 0x60000008+0x100*i),
		/* Digital Interrupt Mask Any Change Block */
		con.sdo_download_uint8 (0x6006, i, 0xFF)
	);
	promises.push(
		/* Number of mapped object for PDO 0 */
		con.sdo_download_uint8 (0x1A00, 0,   nb_bl_in),
		/* Valid COB - enable PDO 0 */
		con.sdo_download_uint32(0x1800, 1, 0x180+con.node_id)
	);
	return Promise.all(promises).catch(err => {
		console.log("Error configuring TX PDO 0: "+err)
	});
}

/* Configure the RX PDO 0 for max 64 digital outputs */
function wago_pdo0rx_output(con, nb_bl_out){
	var promises = [
		/* Invalid COB - disable PDO 0 */
		con.sdo_download_uint32(0x1400, 1, 0x80000000),
		/* Transfer type */
		con.sdo_download_uint8 (0x1400, 2, 255),
		/* Number of mapped object for PDO 0 */
		con.sdo_download_uint8 (0x1600, 0,   0),
	]
	for(var i=1; i<=nb_bl_out; ++i)	promises.push(
		/* Mapped object */
		con.sdo_download_uint32(0x1600, i, 0x62000008+0x100*i),
		/* Error Mode Digital Output 8-Bit Block */
		con.sdo_download_uint8 (0x6206, i, 0xFF),
		/* Error Value Digital Output 8-Bit Block */
		con.sdo_download_uint8 (0x6207, i, 0xFF)
	);
	promises.push(
		/* Number of mapped object for PDO 0 */
		con.sdo_download_uint8 (0x1600, 0, nb_bl_out),
		/* Valid COB - enable PDO 0 */
		con.sdo_download_uint32(0x1400, 1, 0x200+con.node_id)
	);
	return Promise.all(promises).catch(err => {
		console.log("Error configuring RX PDO 0: "+err)
	});
}

function handleInputs(con){
	con.pdo_recv(function(pdoid, data){
		v2 = new Uint8Array(data);
		console.log("RECV: "+pdoid+" "+v2[0]);
	});
}

function randomOutputs(con, nb_bl_out){
	/* Test to switch the digital output */
	t = new ArrayBuffer(nb_bl_out);
	v = new Uint8Array(t);
	setInterval(function() {
		for(var i=0; i<nb_bl_out; ++i)
			v[i] = 1 << Math.floor(Math.random()*8);
		node.pdo_send(0, t);
	}, 100);
}

node.sdo_upload_uint8(0x6000, 0).then(u8 => {
	console.log("Number inputs: "+u8);
	wago_pdo0tx_input(node, u8, 1).then(test => {
		console.log("Success configuring TX PDO 0");
		handleInputs(node);
	});
},err => {
	console.log("Error reading the number of inputs: "+err);
});

node.sdo_upload_uint8(0x6200, 0).then(u8 => {
	console.log("Number outputs: "+u8);
	wago_pdo0rx_output(node, u8, 1).then(test => {
		console.log("Success configuring RX PDO 0");
		node.nmt_send(co.NMT_OPERATIONAL);
		randomOutputs(node, u8);
	});
},err => {
	console.log("Error reading the number of outputs: "+err);
});

node.heartbeat_str(function(state){
	console.log("Node state:", state);
});

setInterval(node.heartbeat, 1000);

