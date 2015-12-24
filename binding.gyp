{
	"targets": [
		{
			"target_name": "prussdrv",
			"sources": [
				"src/prussdrv.cpp",
				"prussdrv/prussdrv.c",
			],
			"include_dirs": [
				"prussdrv",
				"<!(node -e \"require('nan')\")"
			],
			"cflags": [
				"-std=c++11",
				"-fpermissive" 
			]
		}
	]
}
