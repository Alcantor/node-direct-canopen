module.exports = require('./build/Release/dcanopen')

module.exports["send_sdo_uint8"] = 
function (can_node, index, subindex, number){
	let data = new ArrayBuffer(1);
	let v8 = new Uint8Array(data);
	v8[0] = number;
	can_node.sdo_download(index, subindex, data, resp => {
		if(resp instanceof Error){
			console.log(resp);
			return;
		}
		console.log("OK: "+index+" "+subindex);
	});
};

module.exports["send_sdo_uint32"] =
function (can_node, index, subindex, number){
	let data = new ArrayBuffer(4);
	let v32 = new Uint32Array(data);
	v32[0] = number;
	can_node.sdo_download(index, subindex, data, resp => {
		if(resp instanceof Error){
			console.log(resp);
			return;
		}
		console.log("OK: "+index+" "+subindex);
	});
};

