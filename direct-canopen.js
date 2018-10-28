dco = require('./build/Release/dcanopen');

function create_node(device, node_id){
	let obj = dco.create_node(device, node_id);
	obj["sdo_download_uint8"] = function (index, subindex, number){
			let data = new ArrayBuffer(1);
			let v8 = new Uint8Array(data);
			v8[0] = number;
			return obj.sdo_download(index, subindex, data);
		}
	obj["sdo_download_uint16"] = function (index, subindex, number){
			let data = new ArrayBuffer(2);
			let v16 = new Uint16Array(data);
			v16[0] = number;
			return obj.sdo_download(index, subindex, data);
		}
	obj["sdo_download_uint32"] = function (index, subindex, number){
			let data = new ArrayBuffer(4);
			let v32 = new Uint32Array(data);
			v32[0] = number;
			return obj.sdo_download(index, subindex, data);
		}
	obj["sdo_upload_uint8"] = function (index, subindex){
			return obj.sdo_upload(index, subindex).then(data => {
				var v8 = new Uint8Array(data);
				return v8[0];
			});
		}
	obj["sdo_upload_uint16"] = function (index, subindex, cb){
			return obj.sdo_upload(index, subindex).then(data => {
				var v16 = new Uint16Array(data);
				return v16[0];
			});
		}
	obj["sdo_upload_uint32"] = function (index, subindex, cb){
			return obj.sdo_upload(index, subindex).then(data => {
				var v32 = new Uint32Array(data);
				return v32[0];
			});
		}
	return obj;
}

module.exports = {
	"create_node": create_node,
	"NMT_OPERATIONAL": 0x01,
	"NMT_STOP": 0x02,
	"NMT_PRE_OPERATIONAL": 0x80,
	"NMT_RESET_NODE": 0x81,
	"NMT_RESET_COMMUNICATION": 0x82
};

