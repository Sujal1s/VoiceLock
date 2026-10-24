import argparse,csv,json
import numpy as np
def summarize(path,column):
    with open(path,newline='') as f:
        values=np.array([float(row[column]) for row in csv.DictReader(f)],dtype=float)
    values=values[np.isfinite(values)&(values>0)]
    if len(values)<100:raise ValueError('Need at least 100 valid game frame times in milliseconds')
    slowest=np.sort(values)[-max(1,int(np.ceil(len(values)*.01))):]
    return dict(frames=len(values),average_fps=1000/float(values.mean()),one_percent_low_fps=1000/float(slowest.mean()))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('disabled');p.add_argument('enabled');p.add_argument('--column',default='MsBetweenPresents');a=p.parse_args()
    off,on=summarize(a.disabled,a.column),summarize(a.enabled,a.column)
    print(json.dumps(dict(disabled=off,enabled=on,average_fps_loss_percent=100*(1-on['average_fps']/off['average_fps']),one_percent_low_loss_percent=100*(1-on['one_percent_low_fps']/off['one_percent_low_fps'])),indent=2))
