import json
import os
import configparser
import argparse
import time
import logging
import datetime

import matplotlib.pyplot as plt
from matplotlib.pyplot import MultipleLocator

class TimeStuff(object):

    def __init__(self,taskstr):
        self.taskstr = taskstr

    def __enter__(self):
        print("Beginning: %s" % self.taskstr, flush=True)
        self.t0 = time.time()

    def __exit__(self, exception_type, exception_val, trace):
        self.t1 = time.time()
        print("Finished: %s in %s seconds" % (self.taskstr, str(self.t1 - self.t0)), flush=True)
        return False

#Command and args-------------------------------------------------------------------

description = """
Plot graphic keys
"""

parser = argparse.ArgumentParser(description=description)
parser.add_argument('-traindir', help='Dir to write to for recording training results', required=True)
parser.add_argument('-model', help='String name for what model config to use', required=True)
parser.add_argument('-outputdir', help='Directory to save file loss.png', required=True)

args = vars(parser.parse_args())

print("Current time: " + str(datetime.datetime.now()))

baseDir = args["traindir"]
model = args["model"]
trainDirs=[model];
outputdir = args["outputdir"]
outputFile=os.path.join(outputdir,"loss.png")

# lossItems={"p0loss":(1.0,2.5),"vloss":(0.3,0.9),"loss":(0,0)} #name,ylim,  0 means default
lossItems={"p0loss":(0,0),"vloss":(0,0),"loss":(0,0)} #name,ylim,  0 means default
# lossTypes=["train","val"]
lossTypes=["train"]
lossKeys=list(lossItems.keys())
nKeys=len(lossKeys)


def readJsonFile(path,lossKeys,lossType):
    d={}
    d["nsamp"]=[]
    for key in lossKeys:
        d[key]=[]
    with open(path,"r") as f:
      filelines=f.readlines()
      for line in filelines:
          if(len(line)<5):
              continue #bad line
          j=json.loads(line)
          all_keys=True
          for key in lossKeys:
            if(not key in j):
              all_keys=False

          if(not all_keys):
            continue

          if(lossType == "val" and "nsamp_train" in j):
             nsamp=j["nsamp_train"]
          elif(lossType == "train" and "nsamp" in j):
              nsamp = j["nsamp"]
          else:
            continue

          d["nsamp"].append(nsamp)
          for key in lossKeys:
              d[key].append(j[key])
    return d

#os.makedirs(outputDir,exist_ok=True)


fig=plt.figure(figsize=(6,4*nKeys),dpi=400)
plt.subplots_adjust(hspace=0.5)
for i in range(nKeys):
    key=lossKeys[i]
    ax=plt.subplot(nKeys,1,i+1)

    plotLim=lossItems[key]
    ax.set_xlabel("nsamp")
    ax.set_ylabel(key)
    ax.set_title(key)

    if(plotLim[0]!=0 or plotLim[1]!=0):
        ax.set_ylim(plotLim[0],plotLim[1])
        if(plotLim[1]-plotLim[0]>2):
            y_major_locator=MultipleLocator(0.5)
            y_minor_locator=MultipleLocator(0.1)
        elif(plotLim[1]-plotLim[0]>0.5):
            y_major_locator=MultipleLocator(0.1)
            y_minor_locator=MultipleLocator(0.02)
        elif(plotLim[1]-plotLim[0]>0.25):
            y_major_locator=MultipleLocator(0.05)
            y_minor_locator=MultipleLocator(0.01)
        elif(plotLim[1]-plotLim[0]>0.10):
            y_major_locator=MultipleLocator(0.02)
            y_minor_locator=MultipleLocator(0.01)
        elif(plotLim[1]-plotLim[0]>0.02):
            y_major_locator=MultipleLocator(0.01)
            y_minor_locator=MultipleLocator(0.002)
        else:
            y_major_locator=MultipleLocator(0.005)
            y_minor_locator=MultipleLocator(0.001)
        ax.yaxis.set_major_locator(y_major_locator)
        ax.yaxis.set_minor_locator(y_minor_locator)

with TimeStuff("Save image"):
     isSingleDir = len(trainDirs)==1
     if(isSingleDir):
         fig.suptitle(trainDirs[0])
     for trainDir in trainDirs:
         for lossType in lossTypes:
             jsonPath=os.path.join(baseDir,trainDir,"metrics_"+lossType+".json")
             jsonData=readJsonFile(jsonPath,lossKeys,lossType)
             if jsonData=={}:
                continue
             for i in range(nKeys):
                 key = lossKeys[i]
                 ax = plt.subplot(nKeys, 1, i + 1)
                 plotLabel=lossType if isSingleDir else trainDir+"."+lossType
                 ax.plot(jsonData["nsamp"], jsonData[key], label=plotLabel)
                 ax.legend(loc="upper right")

     #plt.show()
     plt.savefig(outputFile)
