{
	"targets": [
		{
			"target_name": "prussdrv",
			"sources": [
				"src/prussdrv.cpp",
				"prussdrv/prussdrv.c",
			],
			"include_dirs": [
				"<!(node -e \"require('nan')\")",
				"prussdrv"
			],
			"cflags": [
				"-fpermissive"
			]
		}
	]
}
