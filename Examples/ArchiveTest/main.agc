#option_explicit

// Project: ArchiveTest 
// Created: 2018-05-09

// show all errors
SetErrorMode(2)
SetPrintSize(20)

// set window properties
SetWindowTitle( "ArchiveTest" )
SetWindowSize( 1024, 768, 0 )
SetWindowAllowResize( 1 ) // allow the user to resize the window

// set display properties
SetVirtualResolution( 1024, 768 ) // doesn't have to match the window
SetOrientationAllowed( 1, 1, 1, 1 ) // allow both portrait and landscape on mobile devices
SetSyncRate( 30, 0 ) // 30fps instead of 60 to save battery
//~ SetVSync(1)
SetScissor( 0,0,0,0 ) // use the maximum available screen space, no black borders
UseNewDefaultFonts( 1 ) // since version 2.0.22 we can use nicer default fonts

Type ItemInfo
	Name as string
	Size as integer
	IsDir as integer
EndType
items as ItemInfo[]

#import_plugin LzmaPlugin as archive

global msgid as integer
msgid = CreateText("")
SetTextPosition(msgid, 0, 100)
SetTextSize(msgid, 20)
#constant NEWLINE	CHR(10)

// result = archive.Clear()						// Removes all contents from archive.

#constant READ_NO_PW		1
#constant READ_WITH_PW		2
#constant WRITE_NO_PW		3
#constant WRITE_WITH_PW		4
#constant DELETE_ITEM		5
#constant LOAD_STAR			6
#constant LOAD_BIG_DATA		7
#constant PLAY_SOUND		8
#constant LOAD_ATLAS		9
#constant CLEAR_ITEMS		12

SetupVirtualButton(READ_NO_PW, "READ" + NEWLINE + "NO PW")
SetupVirtualButton(READ_WITH_PW, "READ" + NEWLINE + "WITH PW")
SetupVirtualButton(WRITE_NO_PW, "WRITE" + NEWLINE + "NO PW")
SetupVirtualButton(WRITE_WITH_PW, "WRITE" + NEWLINE + "WITH PW")
SetupVirtualButton(DELETE_ITEM, "DELETE" + NEWLINE + "ITEM")
SetupVirtualButton(LOAD_STAR, "LOAD" + NEWLINE + "STAR")
SetupVirtualButton(LOAD_BIG_DATA, "LOAD" + NEWLINE + "BIG DATA")
SetupVirtualButton(PLAY_SOUND, "PLAY" + NEWLINE + "SOUND")
SetupVirtualButton(LOAD_ATLAS, "LOAD" + NEWLINE + "ATLAS")
SetupVirtualButton(CLEAR_ITEMS, "CLEAR" + NEWLINE + "ITEMS")

Function SetupVirtualButton(id as integer, text as string)
	AddVirtualButton(id, 40 + (id - 1) * 80, 60, 80)
	SetVirtualButtonText(id, text)
EndFunction

remstart
Set up a sliding progress bar that's controlled by a tween chain that's passed to the plugin.
In order to work properly, the duration of progress tweens needs to be 100 and have a delay of 0.
The tween chain will be updated using the progress percentage rather than elapsed time.
A Chain is used because SetTweenChainTime is used to set the progress rather than UpdateTweenChain.
remend
global progressSpriteID as integer
progressSpriteID = CreateSprite(CreateImageColor(255, 0, 0, 255))
#constant PROGRESS_X		12
#constant PROGRESS_Y		740
#constant PROGRESS_WIDTH	1000
#constant PROGRESS_HEIGHT	20
// Start to the left of the starting X.
SetSpritePosition(progressSpriteID, PROGRESS_X - PROGRESS_WIDTH, PROGRESS_Y)
SetSpriteSize(progressSpriteID, PROGRESS_WIDTH, PROGRESS_HEIGHT)
SetSpriteScissor(progressSpriteID, PROGRESS_X, PROGRESS_Y, PROGRESS_X + PROGRESS_WIDTH, PROGRESS_Y + PROGRESS_HEIGHT)
progressTweenID as integer
progressTweenID = CreateTweenSprite(100)
SetTweenSpriteX(progressTweenID, GetSpriteX(progressSpriteID), PROGRESS_X, TweenLinear())
progressChainID as integer
progressChainID = CreateTweenChain()
AddTweenChainSprite(progressChainID, progressTweenID, progressSpriteID, 0)

archive.SetProgressTweenChain(progressChainID)

// This ball drops down once.
global ballSpriteID as integer
global ballChainIDOnce as integer
ballTweenID as integer
ballSpriteID = CreateSprite(LoadImage("ball.png"))
SetSpritePosition(ballSpriteID, 896, 100)
ballChainIDOnce = CreateTweenChain()
ballTweenID = CreateTweenSprite(1)
SetTweenSpriteY(ballTweenID, 100, 300, TweenBounce())
AddTweenChainSprite(ballChainIDOnce, ballTweenID, ballSpriteID, 0)

archive.AddTweenChain(ballChainIDOnce)

// This ball bounces repeatedly.
ballChainID as integer
ballSpriteID = CreateSprite(LoadImage("ball.png"))
SetSpritePosition(ballSpriteID, 960, 100)
ballChainID = CreateTweenChain()
ballTweenID = CreateTweenSprite(1)
SetTweenSpriteY(ballTweenID, 100, 300, TweenBounce())
AddTweenChainSprite(ballChainID, ballTweenID, ballSpriteID, 0)
ballTweenID = CreateTweenSprite(1)
SetTweenSpriteY(ballTweenID, 300, 100, TweenLinear())
AddTweenChainSprite(ballChainID, ballTweenID, ballSpriteID, 0)
PlayTweenChain(ballChainID)

archive.AddRepeatingTweenChain(ballChainID)

// Atlas/subimage testing
global atlasImage as integer
global atlasSubImage as integer
global atlasSprite as integer
atlasSprite = CreateSprite(0)
SetSpriteSize(atlasSprite, 64, 64)
SetSpritePosition(atlasSprite, 960, 400)
SetSpriteVisible(atlasSprite, 0)

//------------------------------------------

Function ReadArchive(archiveName as string, password as string)
	AddMessage("Reading from " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToRead(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	if archiveID
		AddMessage("Path: " + archive.GetFilePath(archiveID))
		AddMessage("Extracted Text: " + archive.GetItemAsString(archiveID, "subfolder/inner.txt"))
		AddMessage(archive.GetItemListJSON(archiveID))
		archive.Close(archiveID)
	endif
EndFunction

Function AddMemblockString(archiveID as integer, itemName as string, text as string)
	AddMessage("Adding " + itemName + ": " + str(archive.SetItemFromString(archiveID, itemName, text)))
EndFunction

Function WriteArchive(archiveName as string, password as string)
	AddMessage("Writing to " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToWrite(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	PlayTweenChain(ballChainIDOnce)
	if archiveID
		AddMemblockString(archiveID, "hello.txt", "Why hello!  How are you? Unix time = " + str(GetUnixTime()))
		AddMemblockString(archiveID, "readme.txt", "Readme!")
		AddMemblockString(archiveID, "sub\test2.txt", "Subfolder test.")
		AddMemblockString(archiveID, "sub/again.txt", "Another subfolder test.")
		AddMessage("Add atlas.image from file: " + str(archive.SetItemFromImageFile(archiveID, "atlas.image", "atlas.png")))
		AddMessage("Add atlas.subimages from file: " + str(archive.SetItemFromFile(archiveID, "atlas.subimages", "atlas subimages.txt")))

		AddMessage("Add star.image from file: " + str(archive.SetItemFromImageFile(archiveID, "star.image", "star.png")))
		AddMessage("Add star.sound from file: " + str(archive.SetItemFromSoundFile(archiveID, "star.sound", "zap.wav")))
		AddMessage("Add file: " + str(archive.SetItemFromFile(archiveID, "zap.wav", "zap.wav")))
		AddMessage("Add BIG RANDOM file: " + str(archive.SetItemFromFile(archiveID, "bigbin", "big.bin")))
		//~ AddMessage(archive.GetItemListJSON(archiveID))
		archive.Close(archiveID)
		AddMessage("Done")
		AddMessage("Moving file to read path: " + str(archive.MoveFileToReadPath(archiveName)))
		// raw: prefix also supported.
		//~ AddMessage("Moving file to read path: " + str(archive.MoveFileToReadPath("raw:" + GetWritePath() + GetFolder() + archiveName)))
	endif
EndFunction

Function DeleteFromArchive(archiveName as string, password as string)
	AddMessage("Writing to " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToWrite(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	if archiveID
		AddMessage("Has star.image: " + str(archive.HasItem(archiveID, "star.image")))
		AddMessage("Delete star.image from file: " + str(archive.DeleteItem(archiveID, "star.image")))
		AddMessage(archive.GetItemListJSON(archiveID))
		archive.Close(archiveID)
	endif	
EndFunction

global spriteID as integer
global imageID as integer

Function LoadStar(archiveName as string, password as string)
	DeleteImage(imageID)
	DeleteSprite(spriteID)
	AddMessage("Reading star image from " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToRead(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	memblock as integer
	if archiveID
		AddMessage("Has star.image: " + str(archive.HasItem(archiveID, "star.image")))
		if archive.HasItem(archiveID, "star.image")
			imageID = archive.GetItemAsImage(archiveID, "star.image")
			AddMessage("star.image ID: " + str(imageID))
			if imageID
				spriteID = CreateSprite(imageID)
				SetSpritePosition(spriteID, 1024 - 60, 0)
			endif
		endif
		archive.Close(archiveID)
	endif
EndFunction

Function LoadBigData(archiveName as string, password as string)
	AddMessage("Reading big data chunk from " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToRead(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	memblock as integer
	if archiveID
		AddMessage("Has bigbin: " + str(archive.HasItem(archiveID, "bigbin")))
		if archive.HasItem(archiveID, "bigbin")
			AddMessage("Extracting bigbin into memblock")
			memblock = archive.GetItemAsMemblock(archiveID, "bigbin")
			if memblock
				AddMessage("memblock = " + str(memblock) + ", size = " + str(GetMemblockSize(memblock)))
				DeleteMemblock(memblock)
			endif
			AddMessage("Extracted bigbin as 'data' file: " + str(archive.ExtractItemToFile(archiveID, "bigbin", "data", 1)))
		endif
		archive.Close(archiveID)
	endif
EndFunction

global soundID as integer

Function PlayArchiveSound(archiveName as string, password as string)
	DeleteSound(soundID)
	AddMessage("Reading sound from " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToRead(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	if archiveID
		AddMessage("Has star.sound: " + str(archive.HasItem(archiveID, "star.sound")))
		if archive.HasItem(archiveID, "star.sound")
			soundID = archive.GetItemAsSound(archiveID, "star.sound")
			AddMessage("star.sound ID: " + str(soundID))
			if soundID
				PlaySound(soundID)
			endif
		endif
		AddMessage("Has zap.wav: " + str(archive.HasItem(archiveID, "zap.wav")))
		if archive.HasItem(archiveID, "zap.wav")
			AddMessage("Extracted zap.wav as newstar.wav: " + str(archive.ExtractItemToFile(archiveID, "zap.wav", "newzap.wav", 1)))
		endif
		archive.Close(archiveID)
	endif
EndFunction

Function LoadAtlas(archiveName as string, password as string)
	DeleteImage(atlasImage)
	DeleteImage(atlasSubImage)
	AddMessage("Reading image atlas from " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToRead(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	SetSpriteVisible(atlasSprite, 0)
	if archiveID
		AddMessage("Has atlas.image: " + str(archive.HasItem(archiveID, "atlas.image")))
		if archive.HasItem(archiveID, "atlas.image")
			atlasImage = archive.GetItemAsImageAtlas(archiveID, "atlas.image", "atlas.subimages")
			if atlasImage
				AddMessage("Showing subimage '2'")
				atlasSubImage = LoadSubImage(atlasImage, "2")
				SetSpriteImage(atlasSprite, atlasSubImage)
				SetSpriteVisible(atlasSprite, 1)
			endif
		endif
		archive.Close(archiveID)
	endif
EndFunction

Function ClearArchive(archiveName as string, password as string)
	AddMessage("Clearing " + archiveName + ", password: '" + password + "'")
	archiveID as integer
	archiveID = archive.OpenToWrite(archiveName, password)
	AddMessage("Archive ID: " + str(archiveID))
	if archiveID
		archive.Clear(archiveID)
		AddMessage(archive.GetItemListJSON(archiveID))
		archive.Close(archiveID)
		AddMessage("Cleared")
	endif
EndFunction

Function ResetDisplay()
	SetTextString(msgid, "")
	SetSpriteX(progressSpriteID, PROGRESS_X - PROGRESS_WIDTH)
EndFunction

do
    Print(ScreenFPS())
    //~ Print("ReadPath: " + GetReadPath())
    //~ Print("WritePath: " + GetWritePath())
    // Handle Input.
    if GetVirtualButtonPressed(READ_NO_PW)
		ResetDisplay()
		ReadArchive("nopw.7z", "")
	elseif GetVirtualButtonPressed(READ_WITH_PW)
		ResetDisplay()
		ReadArchive("pw.7z", "password")
	elseif GetVirtualButtonPressed(WRITE_NO_PW)
		ResetDisplay()
		WriteArchive("out.7z", "")
	elseif GetVirtualButtonPressed(WRITE_WITH_PW)
		ResetDisplay()
		WriteArchive("out.7z", "password")
	elseif GetVirtualButtonPressed(DELETE_ITEM)
		ResetDisplay()
		DeleteFromArchive("out.7z", "")
	elseif GetVirtualButtonPressed(LOAD_STAR)
		ResetDisplay()
		LoadStar("out.7z", "")
	elseif GetVirtualButtonPressed(LOAD_BIG_DATA)
		ResetDisplay()
		LoadBigData("out.7z", "")
	elseif GetVirtualButtonPressed(PLAY_SOUND)
		ResetDisplay()
		PlayArchiveSound("out.7z", "")
	elseif GetVirtualButtonPressed(LOAD_ATLAS)
		ResetDisplay()
		LoadAtlas("out.7z", "")
	elseif GetVirtualButtonPressed(CLEAR_ITEMS)
		ResetDisplay()
		ClearArchive("out.7z", "")
	endif
	if GetRawKeyPressed(GetRawLastKey())
		exit
	endif
	if not GetTweenChainPlaying(ballChainID)
		PlayTweenChain(ballChainID)
	endif
	UpdateAllTweens(GetFrameTime())
    Sync()
loop
end

Function AddMessage(s as string)
	SetTextString(msgid, GetTextString(msgid) + s + NEWLINE)
EndFunction
